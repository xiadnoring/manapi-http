#include <memory.h>
#include <fstream>
#include <memory>

#include "ManapiFetch.hpp"
#include "ManapiString.hpp"
#include "ManapiHttp.hpp"
#include "ManapiTime.hpp"
#include "http/ManapiHttpMime.hpp"
#include "http/ManapiBaseHttp.hpp"
#include "http/ManapiHttpRequest.hpp"
#include "http/ManapiHttpResponse.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "fs/ManapiFileStream.hpp"
#include "std/ManapiParallelRun.hpp"
#include "std/ManapiEasyCancelToken.hpp"
#include "ext/ManapiMustache.hpp"
#include "crypto/ManapiCryptoUtils.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/ManapiHttpInternal.hpp"
#include "../include/ManapiDefaultErrors.hpp"
#include "../include/ManapiHttpStructs.hpp"
#include "std/ManapiTimer.hpp"
#include "std/ManapiChannel.hpp"

#ifdef MANAPIHTTP_FETCH_SUPPORT
struct response_proxy_data_t {
    int64_t content_length;
    std::shared_ptr<manapi::net::fetch> fetch;
    std::unique_ptr<manapi::net::http::response> resp;
};
#endif


static std::string manapi__generate_cache_name(const std::string &file, const std::string &ext) {
    std::string name = std::format("{}-{:%Y_%m_%d_%H_%M_%S}-{}.{}", manapi::fs::path::basename(std::forward<const std::string&> (file)),
                                   manapi::time::current_time (true), manapi::string::random(25), ext);

    return std::move(name);
}

static manapi::future<manapi::status> manapi__compress_file(std::shared_ptr<manapi::net::http::server> site,
                                                                                        manapi::net::http::internal::file_fd_t *fdata,
                                                                                        std::string folder,
                                                                                        manapi::net::http::response_features_t *features) {
    std::string cached_path;
    manapi::ev::unique_file cached_fd;
    bool cache_prev_found;

    try {
        {
            auto res = co_await site->get_compressed_cache_file(fdata->fpath, features->compress, fdata->flastwrite);
            cache_prev_found = res.ok();
            if (res.ok())
                cached_path = res.unwrap();
            else {
                if (res.code() != manapi::ERR_NOT_FOUND)
                    co_return manapi::status_unavailable("busy");
            }
        }

        while (true) {
            if (!cache_prev_found) {
                auto zres = site->lock_cache_file(std::string{fdata->fpath}, std::string{features->compress});

                try {
                    if (!zres.ok()) {
                        zres = manapi::status_unavailable("busy");
                    } else {

                        try {
                            manapi::unwrap(
                                    co_await manapi::fs::async_mkdir(folder, manapi::ev::IRWXU|manapi::ev::IRGRP|manapi::ev::IXGRP, true));
                        }
                        catch (std::exception const &e) {
                            manapi_log_error("%s due to %s", "mkdir cache directory failed", e.what());
                            zres = manapi::status_internal("mkdir cache directory failed");
                        }

                        if (zres.ok()) {
                            cached_path = manapi::fs::path::join(folder,
                                                                 ::manapi__generate_cache_name(fdata->fpath, features->compress));
                            cached_fd = manapi::unwrap(
                                    co_await manapi::fs::async_open(cached_path,
                                                                    manapi::ev::FS_O_RDWR | manapi::ev::FS_O_CREAT,
                                                                    0755));

                            auto compress_ctx = features->compressor ();
                            auto compress_res = co_await manapi::compress::compress_file(compress_ctx.get(), fdata->fd.get(), cached_fd.get());
                            if (!compress_res.ok()) {
                                zres = std::move(compress_res);
                            } else {

                                zres = co_await site->set_compressed_cache_file(fdata->fpath, cached_path, features->compress,
                                                                                fdata->flastwrite);
                                if (!zres.ok()) {
                                    zres = manapi::status_unavailable("busy");
                                }
                            }
                        }
                    }
                }
                catch (std::exception const &e) {
                    manapi_log_error("%s failed due to %s", "file compress", e.what());
                    zres = manapi::status_internal("file compress");
                }

                site->unlock_cache_file(fdata->fpath, features->compress);

                if (!zres.ok()) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s failed due to %.*s",
                                     "file compress", zres.msg().size(), zres.msg().data());
                    co_return manapi::status_unavailable("busy");
                }
            } else {
                auto cached_fd_res = co_await manapi::fs::async_open(cached_path, manapi::ev::FS_O_RDONLY, 0755);
                if (cached_fd_res.syserr() == manapi::ev::ERR_NOENT) {
                    cache_prev_found = false;
                    continue;
                }

                cached_fd = cached_fd_res.unwrap();
            }

            struct fmeta_t {
                bool found;
                uint64_t st_size;
            } fmeta{};

            fmeta.found = false;

            manapi::unwrap(
                co_await manapi::fs::async_fstat(cached_fd.get(), [&fmeta](manapi::ev::stat_t *stat) -> void {
                    if (stat) {
                        fmeta.st_size = stat->st_size;
                        fmeta.found = true;
                    }
            }, manapi::ctokens::timeout(5000)));

            if (!fmeta.found) {
                co_return manapi::status_unavailable("not found");
            }

            fdata->fd = std::move(cached_fd);
            fdata->fpath = std::move(cached_path);
            fdata->fsize = static_cast<std::size_t>(fmeta.st_size);
            fdata->flags |= manapi::net::http::internal::FILE_FD_FLAG_META;

            break;
        }

        co_return manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("compress:Compress file filed due to %s", e.what());
    }

    co_return manapi::status_internal("compress:Something gets wrong");
}

void manapi__send_error_response(manapi::net::http::internal::uq_handle_data_t cdata, int status);

static int manapi__handle_request_stringify_ip (manapi::net::http::manapi_socket_information *inf, manapi::net::worker::base *w, manapi::net::worker::connection *conn) {
    auto ipdata = w->ipdata(conn);
    auto const sa = reinterpret_cast <struct sockaddr *> (ipdata->client.data);
    auto res = manapi::net::http::strinfigy_ip(sa);
    if (!res.ok())
        return res.code();

    auto val = res.unwrap();

    inf->ip = std::move(val.first);
    inf->port = val.second;

    return manapi::ERR_OK;
}



static void manapi__handle_income_request_err (std::unique_ptr<manapi::net::http::response> res, std::exception_ptr err) {
    manapi::net::http::internal::uq_handle_data_t cdata (res->connection_data_release());
    char msg[256];
    std::size_t msg_size = sizeof (msg);
    manapi::extract_exception_ptr(std::move(err), nullptr, msg, &msg_size);
    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %.*s",
                     "an error occurred while processing the HTTP request",
                     msg_size, msg);
    manapi__send_error_response(std::move(cdata), manapi::net::http::SERVICE_UNAVAILABLE_503);
    return;
}

static manapi::status manapi__execute_user_callback (manapi::net::http::handler_template_t &handle, manapi::net::http::request *req,
                                                     manapi::net::http::response *resp, std::move_only_function<void(std::exception_ptr err)> after_work) {
    try {
        if (handle.is_async_cb()) {
            auto err = handle.async_cb();
            if (!err.ok())
                return err.err();

            manapi::async::run(err.unwrap()->operator()(*req, *resp), std::move(after_work));
        }
        else if (handle.is_sync_cb()) {
            auto err = handle.sync_cb();
            if (!err.ok())
                return err.err();

            std::unique_ptr<decltype(after_work)> after_work_uq( new (std::nothrow) decltype(after_work)(std::move(after_work)));
            if (!after_work_uq)
                return manapi::status_resource_exhausted();

            manapi::net::http::uresponse uresp (resp, std::move(after_work_uq));

            try {
                err.unwrap()->operator()(*req, std::move(uresp));
            }
            catch (std::exception const &e) {
                manapi_log_trace("%s due to %s", "http:User cb failed", e.what());
            }
        }

        return manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s due to %s", "http:User cb failed", e.what());
        return manapi::status_internal("http:User cb failed");
    }
}

static void manapi__handle_income_request_next (manapi::net::http::handler_template_t *handler, std::unique_ptr<manapi::net::http::response> res, uint32_t index) {
    auto const cdata_ptr = res->connection_data();
    auto const res_ptr = res.get();

    auto &layer = cdata_ptr->router->layer;
    if (index >= 0 && index < layer.size()) {
        auto &layer_handler = layer[index]->handler;
        auto status = manapi__execute_user_callback (layer_handler, res_ptr->req(), res_ptr,
                 [handler, res = std::move(res), index = index + 1] (std::exception_ptr err) mutable -> void {
                     if (err)
                         return manapi__handle_income_request_err(std::move(res), std::move(err));

                     if (!res->req()->propagation())
                         // skip other layers and handlers
                         manapi::net::http::internal::send_response(std::move(res));
                     else
                         manapi__handle_income_request_next(handler, std::move(res), index);
                 });

        return;
    }


    if (handler) {
        auto status = manapi__execute_user_callback (*handler, res_ptr->req(), res_ptr,
                     [res = std::move(res)] (std::exception_ptr err) mutable -> void {
                         if (err)
                             return manapi__handle_income_request_err(std::move(res), std::move(err));

                         try {
                             manapi::net::http::internal::send_response(std::move(res));
                         }
                         catch (const std::exception &e) {
                             manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %s",
                                              "send_response", e.what());
                         }
                     });
    }
    else {
        manapi::net::http::internal::send_response (std::move(res));
    }
}

static void manapi__handle_income_request(manapi::net::http::internal::uq_handle_data_t cdata, int status) {
    try {
        // handler function not be found
        if (!cdata->router->handler) {
            // check exists static folder/file
            if (cdata->router->statics) {

                // if statics exists
                std::string path;


                auto maxsize = cdata->req_data->divided >= 0
                               ? static_cast<std::size_t>(cdata->req_data->divided) : cdata->req_data->path.size();

                size_t path_reserved = 0;

                for (size_t i = cdata->router->statics_parts_len; i < maxsize; i++) {
                    path_reserved += sizeof (manapi::fs::path::delimiter);
                    path_reserved += cdata->req_data->path[i].size();
                }

                path.reserve(path_reserved);

                for (size_t i = cdata->router->statics_parts_len; i < maxsize; i++) {
                    path += manapi::fs::path::delimiter;
                    path += cdata->req_data->path[i];
                }

                path = manapi::fs::path::join(cdata->router->statics->folder, path);

                if (!path.starts_with(cdata->router->statics->folder)
                    || (path.size() != cdata->router->statics->folder.size() && (path[cdata->router->statics->folder.size()] != manapi::fs::path::delimiter))) {

                    manapi__send_error_response(std::move(cdata), manapi::net::http::BAD_REQUEST_400);
                    return;
                }

                manapi::async::run([status, cdata = std::move(cdata), path = std::move(path)] () mutable
                                           -> manapi::future<> {
                    try {
                        struct fmeta_t {
                            uint64_t st_mode;
                            uint64_t st_size;
                            std::chrono::time_point<std::chrono::system_clock> st_lastwrite;
                            bool exists;
                        } fmeta{};

                        manapi::ev::unique_file fd;

                        {
                            auto fd_res = co_await manapi::fs::async_open(path, manapi::ev::FS_O_RDONLY, 0755,
                                                                          manapi::ctokens::timeout(5000));

                            if (!fd_res.ok()) {
                                if (fd_res.syserr() == manapi::ev::ERR_NOENT) {
                                    // not found
                                }
                                else {
                                    fd_res.unwrap();
                                }
                            }
                            else {
                                fd = fd_res.unwrap();
                            }
                        }

                        if (fd.has_value()) {
                            manapi::unwrap(co_await manapi::fs::async_fstat(fd.get(), [&fmeta](manapi::ev::stat_t *stat)
                                    -> void {
                                if (stat) {
                                    fmeta.st_mode = stat->st_mode;
                                    fmeta.st_size = stat->st_size;
                                    fmeta.st_lastwrite = std::chrono::system_clock::time_point(std::chrono::seconds{
                                            stat->st_mtim.tv_sec}/* + std::chrono::nanoseconds{mtime.tv_nsec} */);
                                    fmeta.exists = true;
                                } else {
                                    fmeta.exists = false;
                                }
                            }, manapi::ctokens::timeout(5000)));

                            if (fmeta.exists) {
                                if (fmeta.st_mode & (manapi::ev::IFREG | manapi::ev::IFLNK)) {
                                    const auto ext = manapi::fs::path::extension(path);
                                    auto const mime = manapi::mime::mime_by_file_extension(ext);
                                    bool const binary = manapi::mime::mime_partitial_data(mime);
                                    auto client = std::make_unique<manapi::net::http::manapi_socket_information>();

                                    if (manapi__handle_request_stringify_ip(client.get(), cdata->worker.get(),
                                                                    cdata->conn.get())) {
                                        /* error */
                                        manapi::async::current()->logger()->error(manapi::ERR_INTERNAL,
                                                                                  "stringify_ip(): ip get failed");
                                        co_return;
                                    }

                                    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "conn:%p is %s:%u",
                                                     cdata->conn.get(), client->ip.data(),
                                                     static_cast<uint32_t>(client->port));

                                    auto const handler = (cdata->router->statics->layer
                                                          && cdata->router->statics->layer->handler)
                                                         ? &cdata->router->statics->layer->handler : nullptr;

                                    cdata->req_data->cdata = cdata.get();

                                    auto req = std::make_unique<manapi::net::http::request>(std::move(client), cdata->req_data,
                                                                               &cdata->conn, cdata->worker);
                                    auto res = std::make_unique<manapi::net::http::response>(cdata.release(), status,
                                                                                std::move(req));

                                    res->compress_enabled(fmeta.st_size < 20 * 1024 * 1024);
                                    res->partial_enabled(binary);
                                    res->fd(std::move(fd), std::move(path)).unwrap();

                                    auto zfd = res->file().unwrap();
                                    zfd->flastwrite = fmeta.st_lastwrite;
                                    zfd->fsize = static_cast<std::size_t>(fmeta.st_size);
                                    zfd->flags |= manapi::net::http::internal::FILE_FD_FLAG_META;

                                    manapi__handle_income_request_next(handler, std::move(res), 0);
                                } else if (fmeta.st_mode & (manapi::ev::IFDIR | manapi::ev::IFCHR | manapi::ev::IFBLK | manapi::ev::IFSOCK)) {
                                    if (cdata) {
                                        manapi__send_error_response(std::move(cdata), manapi::net::http::FORBIDDEN_403);
                                    }
                                    co_return;
                                }
                            }
                        }

                        if (cdata) {
                            manapi__send_error_response(std::move(cdata), manapi::net::http::NOT_FOUND_404);
                        }
                    }
                    catch (std::exception const &e) {
                        manapi_log_trace(e.what());
                        co_return;
                    }
                });
            }
            else {
                manapi__send_error_response(std::move(cdata), manapi::net::http::NOT_FOUND_404);
            }
            return;
        }

        auto client = std::make_unique<manapi::net::http::manapi_socket_information>();

        if (manapi__handle_request_stringify_ip(client.get(),
                                        cdata->worker.get(), cdata->conn.get())) {
            /* error */
            manapi::async::current()->logger()->error(manapi::ERR_INTERNAL, "stringify_ip(): ip get failed");
            return;
        }

        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "conn:%p is %s:%u",
                         cdata->conn.get(), client->ip.data(), static_cast<uint32_t>(client->port));

        auto handler = &cdata->router->handler->handler;

        cdata->req_data->cdata = cdata.get();

        auto req = std::make_unique<manapi::net::http::request> (std::move(client), cdata->req_data, &cdata->conn, cdata->worker);
        auto res = std::make_unique<manapi::net::http::response> (cdata.release(), status, std::move(req));

        manapi__handle_income_request_next (handler, std::move(res), 0);

        return;
    }
    catch (const manapi::exception &e) {
        switch (e.err_num()) {
            case manapi::ERR_ABORTED: return;
            default: manapi_log_trace("%s failed due to %s", "http:handle requests", e.what());
        }
    }
    catch (const std::exception &e) {
        manapi_log_trace("%s failed due to %s", "http:handle requests", e.what());
    }

    manapi__send_error_response(std::move(cdata), manapi::net::http::SERVICE_UNAVAILABLE_503);
}

void manapi__send_error_response(manapi::net::http::internal::uq_handle_data_t cdata, int status) {
    if (!cdata->router || cdata->router->error.empty()) {
        return;
    }

    auto &b = cdata->router->error.back();
    cdata->router->handler = b.handler;
    cdata->router->layer.resize(b.layer_depth);
    cdata->router->error.pop_back();

    assert((cdata));

    manapi__handle_income_request(std::move(cdata), status);
}

manapi::net::http::internal::handle_data_t::~handle_data_t() {
    // std::cout << "DESTRYOED\n";
}

std::string manapi::net::http::internal::generate_default_page(int status, std::string_view msg) {
    return std::format("<html>\n\t<head>\n\t\t"
                            "<title>{0} {1}</title>\n\t</head>\n\t<body>\n\t\t<center>\n\t\t\t"
                            "<h1>{0} {1}</h1>\n\t\t</center>\n\t\t<hr>\n\t\t"
                            "<center>{3}/{2}</center>\n\t"
                            "</body>\n</html>", status,
                            msg, MANAPIHTTP_VERSION, MANAPIHTTP_NAME);
}

void manapi::net::http::internal::send_response(std::unique_ptr<response> res) {
    try {
        std::string response;
        std::string compressed;
        manapi::status st;

        response_features_t features = {
            .compress = res->compress(),
            .compressor = nullptr,
            .replacers = std::move(res->replacers())
        };

        auto const cdata = res->connection_data();

        if (!features.compress.empty() && !(res->partial_enabled() && res->contains_ranges())) {
            auto pool = http::server::cast(cdata->worker->site().get());
            features.compressor = pool->compressor (features.compress);
        }


        // set time
        res->header(std::string{H_DATE}, std::format("{:%a, %d %b %Y %H:%M:%S} GMT", std::chrono::time_point_cast<std::chrono::seconds>(manapi::time::current_time(false).get_sys_time()))).unwrap();
        if (res->request_data()->http < versions::HTTP_v2) {
            auto const keepalive = cdata->worker->config()->keep_alive;
            if (keepalive) {
                if (! (st = res->header(std::string{H_CONNECTION}, std::string{H_KEEP_ALIVE}))) {
                    goto finish;
                }
            }
            else {
                if (! (st = res->header(std::string{H_CONNECTION}, "close"))) {
                    goto finish;
                }
            }
        }

        switch (res->data_type()) {
            case internal::RESPONSE_FILE:
                manapi::async::run(send_response_file(std::move(res), std::move(features)));
            break;
            case internal::RESPONSE_TEXT:
                manapi::async::run(send_response_text(std::move(res), std::move(features)));
            break;
            case internal::RESPONSE_PROXY:
                manapi::async::run(send_response_proxy(std::move(res), std::move(features)));
            break;
            case internal::RESPONSE_FORMDATA:
                manapi::async::run(send_response_formdata(std::move(res), std::move(features)));
            break;
            case internal::RESPONSE_ASYNC_CALLBACK:
                send_response_async_cb(std::move(res), std::move(features));
            break;
            case internal::RESPONSE_SYNC_CALLBACK:
                send_response_sync_cb(std::move(res), std::move(features));
            break;
            case internal::RESPONSE_STREAM:
                send_response_stream_cb(std::move(res), std::move(features));
            break;
            case internal::RESPONSE_SLICE:
                manapi::async::run(send_response_slice(std::move(res), std::move(features)));
            break;
            default: {
                auto const resptr = res.get();
                manapi::async::run<int>(mask_response(resptr, true),
                    [res = std::move(res)] (std::exception_ptr err, int *result)
                    -> void {
                        if (err) {
                            /* failed */
                            return;
                        }
                        res->connection_data()->cb->call(result && *result == ERR_OK);
                });
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "send_response()", e.what());
    }
finish:
    return;
}

manapi::future<void> manapi::net::http::internal::send_response_file(std::unique_ptr<response> res, response_features_t features) {
    manapi::status st = manapi::status_ok();

    auto resfile = std::move(*res->file().unwrap());
    auto const cdata = res->connection_data();

    if (!(resfile.flags & manapi::net::http::internal::FILE_FD_FLAG_META)) {
        try {
            if (!resfile.fd.has_value()) {
                resfile.fd = manapi::unwrap(
                        co_await manapi::fs::async_open(resfile.fpath, manapi::ev::FS_O_RDONLY, 0755,
                                                        res->req()->cancellation().sub().tm(5000)));
            }

            manapi::unwrap(
                    co_await manapi::fs::async_fstat(resfile.fd.get(), [&resfile](manapi::ev::stat_t *stat) -> void {
                        if (stat) {
                            resfile.flags |= internal::FILE_FD_FLAG_META;
                            resfile.fsize = stat->st_size;
                            resfile.flastwrite = std::chrono::system_clock::time_point(std::chrono::seconds{
                                    stat->st_mtim.tv_sec}/* + std::chrono::nanoseconds{mtime.tv_nsec} */);
                        }
                    }, res->req()->cancellation().sub().tm(5000)));
        }
        catch (std::exception const &e) {
            manapi_log_trace(e.what());
        }

        if (!(resfile.flags & manapi::net::http::internal::FILE_FD_FLAG_META)) {

            uq_handle_data_t uq_cdata (res->connection_data_release());
            manapi__send_error_response(std::move(uq_cdata), http::NOT_FOUND_404);

            co_return;
        }
    }

    try {
        if (features.compressor) {
            if (features.replacers) {
                manapi_log_error("send_response_file():replacers can not be using during compress");
                co_return;
            }

            bool is_ok = false;

            try {
                auto rhs = co_await ::manapi__compress_file(std::dynamic_pointer_cast<manapi::net::http::server>(cdata->worker->site()), &resfile,
                    http::server::cast(cdata->worker->site().get())->config_cache_dir(), &features);
                if (rhs.ok()) {
                    is_ok = true;
                }
                else {
                    if (rhs.code() != manapi::ERR_UNAVAILABLE) {
                        rhs.unwrap();
                    }
                }
            }
            catch (std::exception const &e) {
                manapi_log_error("compress:Compress file failed due to %s", e.what());
            }

            if (is_ok) {
                if (! ( st = res->header(std::string{H_CONTENT_ENCODING}, features.compress) )) {
                    co_return;
                }
            }
        }

        auto status = fs::fstream::create (std::move(resfile.fd), res->req()->cancellation().sub());
        if (!status) {
            uq_handle_data_t uq_cdata (res->connection_data_release());
            manapi__send_error_response(std::move(uq_cdata), http::INTERNAL_SERVER_ERROR_500);
            co_return;
        }

        auto f = status.unwrap();

        // set headers

        std::string_view mimetype = mime::mime_by_file_path(resfile.fpath);
        std::vector<replace_founded_item> replacers;

        if (!res->headers().contains(H_CONTENT_TYPE)) {
            if (mimetype.starts_with("text/")) {
                auto mimegen = stringify_header_value({{mimetype, {{"charset", "UTF-8"}}}});
                if (!( st = res->header(std::string{H_CONTENT_TYPE}, std::move(mimegen)))) {
                    co_return;
                }
            }
            else {
                if (!(st = res->header(std::string{H_CONTENT_TYPE}, std::string{mimetype}))) {
                    co_return;
                }
            }
        }

        if (features.replacers) {
            if (resfile.fsize <= 65536) {
                std::string b;
                b.resize(resfile.fsize);
                auto const recv_result = co_await f->fread(b.data(), b.size());
                if (recv_result < 0) {
                    uq_handle_data_t uq_cdata (res->connection_data_release());
                    manapi__send_error_response(std::move(uq_cdata), http::INTERNAL_SERVER_ERROR_500);
                }
                else {
                    b.resize(static_cast<std::size_t> (recv_result));
                    res->text(std::move(b)).unwrap();
                    manapi::async::run(send_response_text(std::move(res), std::move(features)));
                }
                co_return;
            }

            manapi_log_error("file is too large to use replacers. max size: %d", 65536);
            co_return;
        }



        // partial enabled
        if (res->partial_enabled() && res->contains_ranges() && res->config()->partial_data_min_size <= resfile.fsize) {
            if (features.compressor) {
                manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s:%s failed due to %s", "send_response_file()", "compressor",
                    "compress with the partial content is not supported");
            }

            res->status(http::PARTIAL_CONTENT_206);
            res->header(std::string{H_ACCEPT_RANGES}, "bytes").unwrap();

            std::size_t start = 0,
                    back = static_cast<std::size_t>(resfile.fsize) - 1,
                    size;

            auto ranges = res->ranges();
            std::size_t const ranges_size = ranges ? ranges->size() : 0;

            switch (ranges_size) {
                case 0:
                    break;
                case 1: {
                    auto &range = ranges->operator[](0);
                    if (range.first != std::numeric_limits<std::size_t>::max()) {
                        start = range.first;
                    }

                    if (range.second != std::numeric_limits<std::size_t>::max()) {
                        back = range.second;
                    }
                    else {
                        back = resfile.fsize - 1;
                    }
                    break;
                }
                default: {
                    manapi_log_error("%s: %s", "send_response_file()", "multi bytes not supported");
                }
            }

            size = back + 1 - start;

            res->header(std::string{H_CONTENT_LENGTH}, std::to_string(resfile.fsize)).unwrap();
            res->header(std::string{H_CONTENT_RANGE}, std::format("bytes {}-{}/{}", start, back, resfile.fsize)).unwrap();

            auto task = mask_response(res.get(), size == 0);
            manapi::async::run<int>(std::move(task),
                [size, start, f, res = std::move(res)] (std::exception_ptr err, int *value) mutable
                -> void {
                    if (err) {
                        return;
                    }

                    if (value && *value == ERR_OK) {
                        // set start position
                        f->seekg(static_cast<ssize_t>(start));
                        // set size and send
                        if (size) {
                            manapi::async::run (send_file(std::move(res), f, static_cast<std::size_t>(size)));
                        }
                    }
            });
        }
        else {
            res->header(std::string{H_CONTENT_LENGTH}, std::to_string(resfile.fsize)).unwrap();

            if (resfile.fsize) {
                auto task = mask_response(res.get(), false);
                manapi::async::run<int>(std::move(task),
                    [res = std::move(res), fileSize = resfile.fsize, replacers = std::move(replacers), f = std::move(f), cdata = std::move(cdata)] (std::exception_ptr err, int *value) mutable
                    -> void {
                        if (err) {
                            return;
                        }

                        if (value && *value == ERR_OK) {
                            if (replacers.empty()) {
                                // without replacers
                                manapi::async::run( send_file(std::move(res), f, static_cast<std::size_t>(fileSize)));
                            }
                            else {
                                manapi::async::run(send_file(std::move(res), f, static_cast<std::size_t>(fileSize)));
                            }
                        }
                });
            }
            else {
                auto task = mask_response(res.get(), true);
                manapi::async::run<int>(std::move(task), [ res = std::move(res)] (std::exception_ptr err, int *result)
                    -> void { if (err) { return; } res->connection_data()->cb->call(result && *result == ERR_OK); });
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "send_response", e.what());
    }
}

manapi::future<void> manapi::net::http::internal::send_response_text(std::unique_ptr<response> res, response_features_t features) {
    try {
        std::string plaintext;
        {
            auto st = res->text();
            if (!st) {
                co_return;
            }

            plaintext = std::move(*st.unwrap());
        }

        if (features.replacers) {
            manapi::kainjow::mustache::data data;
            for (auto &v : *features.replacers) {
                data.set(v.first, v.second);
            }

            std::string input = std::move(plaintext);
            manapi::kainjow::mustache::mustache tmpl (input);
            plaintext = tmpl.render(data);
        }

        if (features.compressor) {
            manapi::slice_ref sv;
            sv.push_back( plaintext.data(), plaintext.size() ).unwrap();
            auto inst = features.compressor ();
            auto st = manapi::compress::compress_string( inst.get(), sv );
            features.compressor = nullptr;
            inst.reset();

            if (st.ok()) {

                res->header(std::string{H_CONTENT_ENCODING}, features.compress).unwrap();

                res->slice( st.unwrap() );
                manapi::async::run ( manapi::net::http::internal::send_response_slice( std::move(res), std::move(features) ) );

                co_return;
            }
        }

        res->header(std::string{H_CONTENT_LENGTH}, std::to_string(plaintext.size())).unwrap();

        if (!res->headers().contains(H_CONTENT_TYPE)) {
            res->header(std::string{H_CONTENT_TYPE}, "text/html; charset=UTF-8").unwrap();
        }

        auto task = mask_response(res.get(), plaintext.empty());
        manapi::async::run<int>(std::move(task),
            [plaintext = std::move(plaintext), res = std::move(res)] (std::exception_ptr err, int *value) mutable
            -> void {
                if (err) {
                    /* failed */
                    return;
                }

                if (value && *value == ERR_OK) {
                    /* ok */
                    if (plaintext.empty()) {
                        res->connection_data()->cb->call(true);
                    }
                    else {
                        manapi::async::run(send_text(std::move(res), std::move(plaintext)));
                    }
                }
        });

        co_return;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "send_response_text()", e.what());
    }
}

manapi::future<void> manapi::net::http::internal::send_response_proxy(std::unique_ptr<response> res, response_features_t features) {
#ifdef MANAPIHTTP_FETCH_SUPPORT
    try {
        auto proxy_err = res->url();
        if (!proxy_err) {
            co_return;
        }

        auto proxy_data = std::make_shared<response_proxy_data_t>();
        if (!proxy_data)
            goto send_error;

        auto proxy_res = manapi::net::fetch::create(std::move(*proxy_err.unwrap()));
        if (!proxy_res)
            goto send_error;

        auto req = res->req();
        proxy_data->fetch = proxy_res.unwrap();
        proxy_data->content_length = 0;
        proxy_data->fetch->method(res->request_data()->method);
        proxy_data->resp = std::move(res);

        auto proxy_setup = std::move(proxy_data->resp->proxy_setup_cb());

        if (proxy_setup) {
            proxy_setup->operator()(proxy_data->fetch);
        }

        auto &client_headers = proxy_data->resp->req()->ref_headers();

        for (const auto &hd : client_headers) {
            if (hd.first.starts_with(":"))
                continue;
            if (hd.first == manapi::net::http::H_HOST)
                continue;
            if (hd.first == manapi::net::http::H_CONNECTION)
                continue;
            if (hd.first == manapi::net::http::H_KEEP_ALIVE)
                continue;

            proxy_data->fetch->send_header( std::string_view (hd.first), std::string_view (hd.second) ).unwrap();
        }


        proxy_data->fetch->recv_async_headers (
            [p = proxy_data.get()](const std::shared_ptr<manapi::net::fetch> &f) mutable
            -> manapi::future<bool> {
                try {
                    p->resp->status_code(p->fetch->status_code());

                    auto it = f->headers().find(H_CONTENT_LENGTH);
                    if (it != f->headers().end()) {
                        char *strend = nullptr;
                        p->content_length = std::strtoll(it->second.data(), &strend, 10);
                        if (!p->resp->header(std::string{H_CONTENT_LENGTH}, it->second))
                            co_return false;
                    }

                    const auto rhs1 = co_await mask_response(p->resp.get(), p->content_length == 0);

                    if (rhs1 != ERR_OK) {
                        co_return false;
                    }

                    co_return true;
                }
                catch (std::exception const &e) {
                    manapi_log_error("%s failed due to %s", "send_response_proxy()", e.what());
                }
                co_return false;
        }).unwrap();

        if (req->has_body()) {
            auto channel = manapi::create_channel(65536);
            auto wchannel = std::make_shared<manapi::channel_send> (channel);
            auto rchannel = std::make_shared<manapi::channel_recv> (channel);

            manapi::async::run <manapi::status> ( req->callback_async([wchannel] (slice_view buffs, bool fin) mutable -> manapi::future<ssize_t> {
                auto zv = manapi::slice::create(buffs.size()).unwrap();
                zv.copy_from(buffs, 0, 0, buffs.size()).unwrap();
                manapi::unwrap(co_await wchannel->send(std::move(zv), fin));
                co_return static_cast<ssize_t>(buffs.size());
            }), [proxy_data] (std::exception_ptr err, manapi::status *status) mutable -> void {});

            proxy_data->fetch->send_async_body([rchannel] (slice_view buffs, bool &fin) mutable -> manapi::future<ssize_t> {
                auto res = manapi::unwrap(co_await rchannel->recv(static_cast<ssize_t>(buffs.size())));
                fin = rchannel->is_finished();
                buffs.copy_from(res, 0, 0, res.size()).unwrap();
                co_return static_cast<ssize_t>(res.size());
            }).unwrap();
        }


        proxy_data->fetch->recv_async_body(
            [p = proxy_data.get()](slice_view buffs, bool fin) mutable
                -> manapi::future<ssize_t> {
            if (p->content_length > 0 && static_cast<std::size_t>(p->content_length) <= buffs.size())
                fin = true;

            auto const cdata = p->resp->connection_data();
            auto rhs = co_await cdata->worker->fwrite (cdata->conn, buffs, fin);

            if (rhs < 0)
                co_return -1;

            p->content_length -= rhs;

            co_return rhs;
        }).unwrap();

        auto task = proxy_data->fetch->perform(proxy_data->resp->req()->cancellation().sub());
        manapi::async::run<manapi::status> (std::move(task), [proxy_data = std::move(proxy_data)]
                (std::exception_ptr err, manapi::status *status) mutable -> void {
                if (status && !status->ok()) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %.*s", "send_response_proxy()",
                        status->msg().size(), status->msg().data());
                    return;
                }

                if (err) {
                    /* failed */
                    char msg[256];
                    std::size_t msg_size = sizeof (msg);
                    manapi::extract_exception_ptr(std::move(err), nullptr, msg, &msg_size);
                    manapi_log_trace("%s failed due to %.*s", "send_response_proxy()", msg_size, msg);
                    return;
                }


                proxy_data->resp->connection_data()->cb->call(true);
            });

        co_return;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "send_response_proxy()", e.what());
    }
    co_return;
#else
    manapi_log_error("%s failed due to %s", "send_response_proxy()", "unimplemented");
#endif
send_error:
    auto cdata = res->connection_data();
    uq_handle_data_t uq_cdata (res->connection_data_release());
    manapi__send_error_response(std::move(uq_cdata), http::INTERNAL_SERVER_ERROR_500);
    co_return;
}

manapi::future<> manapi::net::http::internal::send_response_formdata(std::unique_ptr<response> res, response_features_t features) {
    try {
        auto formdata_err = res->formdata();
        if (!formdata_err) {
            co_return;
        }
        auto formdata = std::make_unique<formdata_send>(std::move(*formdata_err.unwrap()));

        if (features.compressor) {
            manapi_log_error("formdata: Compression isn't supported");
            co_return;
        }

        if (features.replacers) {
            manapi_log_error("formdata: Replacers isn't supported");
            co_return;
        }

        auto size = manapi::unwrap(co_await formdata->payload_size());

        auto boundary = formdata->generate_boundary();
        size += formdata->multipart_size(boundary.size()).unwrap();

        res->header(std::string{H_CONTENT_LENGTH}, std::to_string(size)).unwrap();
        res->header(std::string{H_CONTENT_TYPE}, stringify_header_value({{"multipart/form-data", {{"boundary", boundary.substr(2)}}}})).unwrap();

        auto task = mask_response(res.get(), false);
        manapi::async::run<int> (std::move(task),
            [boundary = std::move(boundary), formdata = std::move(formdata), res = std::move(res)] (std::exception_ptr err, int *result) mutable
            -> void {
                if (err) {
                    /* failed */
                    return;
                }


                if (result && *result == ERR_OK) {
                    auto task = formdata->data2multipart(std::move(boundary),
                        res->config()->buffer_size,
                        [res = res.get()] (manapi::slice_view slice, bool fin)
                        -> future<manapi::status> {
                            auto const cdata = res->connection_data();

                            if (co_await cdata->worker->fwrite(cdata->conn, slice, fin) < 0) {
                                co_return status_aborted("worker:Failed to write");
                            }

                            co_return status_ok();
                        });

                    manapi::async::run<manapi::status>(std::move(task),
                        [res = std::move(res), formdata = std::move(formdata)] (std::exception_ptr err, manapi::status *status) mutable
                        -> void {
                            if (err) {
                                return;
                            }

                            if (!status->ok()) {
                                manapi_log_trace(manapi::debug::LOG_TRACE_HIGH,
                                    "%s failed due to %.*s", "send_response_formdata",
                                    status->msg().size(), status->msg().data());
                                return;
                            }

                            res->connection_data()->cb->call(true);
                    });
                }
            });

        co_return;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "send_response_formdata()", e.what());
    }
}

manapi::future<> manapi::net::http::internal::send_response_slice(std::unique_ptr<response> res, response_features_t features) {
    try {
        manapi::slice sv;
        {
            auto st = res->slice();
            if (!st.ok()) {
                co_return;
            }
            sv = std::move(*st.unwrap());
        }

        if (features.replacers) {
            manapi_log_debug("send_response_slice: replacers are not supported");
        }

        if (features.compressor) {

            auto inst = features.compressor ();
            auto st = manapi::compress::compress_string( inst.get(), sv );
            if (st.ok()) {
                res->header(std::string{H_CONTENT_ENCODING}, features.compress).unwrap();
                sv = st.unwrap();
            }

            features.compressor = nullptr;
        }

        res->header(std::string{H_CONTENT_LENGTH}, std::to_string(sv.size())).unwrap();

        if (!res->headers().contains(H_CONTENT_TYPE)) {
            res->header(std::string{H_CONTENT_TYPE}, "text/html; charset=UTF-8").unwrap();
        }

        auto task = mask_response(res.get(), sv.empty());
        manapi::async::run<int>(std::move(task),
            [sv = std::move(sv), res = std::move(res)] (std::exception_ptr err, int *value) mutable
            -> void {
                if (err) {
                    return;
                }

                if (value && *value == ERR_OK) {
                    if (sv.empty()) {
                        res->connection_data()->cb->call(true);
                    }
                    else {
                        manapi::async::run(send_slice(std::move(res), std::move(sv)));
                    }
                }
        });

        co_return;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "send_response_text()", e.what());
    }
}

bool http_v1_1_is_chunked_data (manapi::net::http::response *resp) {
    if (resp->connection_data()->conn->version == manapi::net::http::versions::HTTP_v1_1
        && !resp->headers().contains(manapi::net::http::H_CONTENT_LENGTH)) {
        return true;
    }
    return false;
}

manapi::future<manapi::status> send_http_v1_1_chunked_data (manapi::net::http::internal::handle_data_t *cdata, manapi::slice_view buffs, bool &finish) {
    try {
        auto const rhs = buffs.size();
        std::string_view const msg = {"0\r\n\r\n"};

        if (rhs) {
            char header[20];

            auto res = std::to_chars(header, header + sizeof (header), rhs, 16);

            if (res.ec != std::errc()) {
                auto errmsg = std::make_error_code(res.ec).message();
                manapi_log_trace("%s due to %.*s", "send_http_v1_1_chunked_data:to_chars failed", errmsg.size(), errmsg.data());
                co_return manapi::status_internal("send_http_v1_1_chunked_data:to_chars failed");
            }

            auto len = static_cast<std::size_t>(res.ptr - header);

            if (len + 2 > sizeof (header) - 1)
                /* TODO: think */
                    goto err;

            memcpy (header + len, static_cast<const char *>("\r\n"), 2);
            len += 2;

            if (co_await cdata->worker->fwrite(cdata->conn, header, len, false) <= 0)
                goto err;

            if (co_await cdata->worker->fwrite(cdata->conn, buffs, false) <= 0)
                goto err;

            if (co_await cdata->worker->fwrite(cdata->conn, msg.data() + 1, 2, false) <= 0)
                goto err;
        }

        if (finish) {
            if (co_await cdata->worker->fwrite(cdata->conn, msg.data(),
                    (msg.size()), true) <= 0)
                goto err;
        }
        co_return manapi::status_ok();
err:
        co_return manapi::status_internal("send_http_v1_1_chunked_data:Write failed");
    }
    catch (std::bad_alloc const &) {
        co_return manapi::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "send_http_v1_1_chunked_data:Failed", e.what());
        co_return manapi::status_internal("send_http_v1_1_chunked_data:Failed");
    }
}

void manapi::net::http::internal::send_response_sync_cb(std::unique_ptr<response> res, response_features_t features) {
    if (features.compressor) {
        manapi_log_error("%s: %s", "send_response_sync_cb()", "Compression isn't supported");
        return;
    }

    if (features.replacers) {
        manapi_log_error("%s: %s", "send_response_sync_cb()", "Replacers isn't supported");
        return;
    }


    if (http_v1_1_is_chunked_data(res.get())) {
        auto err = res->header(std::string{http::H_TRANSFER_ENCODING}, "chunked");
        if (!err)
            return;
    }
    auto task = mask_response(res.get(), false);
    manapi::async::run<int> ( std::move(task),
        [res = std::move(res)] (std::exception_ptr err, int *result) mutable
        -> void {
            try {
                if (err) {

                    return;
                }

                if (result && *result == ERR_OK) {
                    auto cb_sync_err = res->callback_sync();
                    if (!cb_sync_err)
                        return;

                    auto cb_sync = (std::move(*cb_sync_err.unwrap()));
                    manapi::async::run ([res = std::move(res), cb_sync = std::move(cb_sync)] () mutable
                        -> manapi::future<> {
                        try {
                            bool finish = false;
                            std::size_t cursor = 0;
                            auto const cdata = res->connection_data();

                            if (http_v1_1_is_chunked_data(res.get())) {
                                auto bufres = manapi::async::current()->memory_fabric().buffer (
                                    std::max<std::size_t>(res->config()->buffer_size, 64));
                                auto buffer = bufres.unwrap();
                                while (!finish) {
                                    auto rhs = cb_sync(buffer.data(), (buffer.size()), finish);
                                    if (rhs < 0) {
                                        manapi_log_trace("send_response_sync_cb():The callback returned an invalid length");
                                        co_return;
                                    }
                                    slice_ref buffs;
                                    if (rhs)
                                        buffs.push_back(buffer.data(), static_cast<std::size_t>(rhs));
                                    auto chunk_err = co_await send_http_v1_1_chunked_data(cdata, buffs, finish);
                                    if (!chunk_err) {
                                        manapi_log_trace("%s failed due to %.*s", "send_response_sync_cb()",
                                            chunk_err.msg().size(), chunk_err.msg().data());
                                        co_return;
                                    }
                                }
                            }
                            else {
                                auto slices = cdata->worker->bufferpool().slice(4096 * 16).unwrap();
                                while (!finish) {
                                    std::size_t total = 0;
                                    for (auto it = slices.begin(); it != slices.end() && !finish; ) {
                                        auto rhs = cb_sync(static_cast<char *>(it.buffer()) + cursor,
                                            (it.size() - cursor), finish);

                                        if (rhs < 0) {
                                            manapi_log_trace("send_response_sync_cb():The callback returned an invalid length");
                                            co_return;
                                        }

                                        cursor += static_cast<std::size_t>(rhs);

                                        if (cursor == it.size()) {
                                            total += cursor;
                                            cursor = 0;
                                            it++;
                                        }
                                    }
                                    if (cursor) {
                                        total += cursor;
                                        cursor = 0;
                                    }

                                    auto slice_vw = slices.subslice(0, total).unwrap();
                                    manapi_log_trace_hard("send size=%d fin=%d", slice_vw.size(), finish);
                                    auto const rhs = co_await cdata->worker->fwrite(
                                        cdata->conn, slice_vw, finish);
                                    manapi_log_trace_hard("fin send");

                                    if (rhs <= 0)
                                        co_return;
                                }
                            }
                        }
                        catch (std::exception const &e) {
                            manapi_log_error("%s failed due to %s", "send_response_sync_cb()", e.what());
                        }
                    });
                }
            }
            catch (std::exception const &e) {
                manapi_log_error("%s failed due to %s", "send_response_sync_cb()", e.what());
            }
        });
}

void manapi::net::http::internal::send_response_stream_cb(std::unique_ptr<response> res, response_features_t features) {
    try {
        if (features.compressor) {
            manapi_log_error("%s failed due to %s", "send_response_stream_cb()", "Compression isn't supported");
            return;
        }

        if (features.replacers) {
            manapi_log_error("%s failed due to %s", "send_response_stream_cb()", "Replacers isn't supported");
            return;
        }

        if (http_v1_1_is_chunked_data(res.get())) {
            auto err = res->header(std::string{http::H_TRANSFER_ENCODING}, "chunked");
            if (!err)
                return;
        }

        auto task = mask_response(res.get(), false);
        manapi::async::run<int> (std::move(task),
            [res = std::move(res)] (std::exception_ptr err, int *result) mutable
            -> void {
                try {
                    if (err) {

                        return;
                    }

                    if (result && *result == ERR_OK) {
                        auto cb_async_err = res->callback_stream();
                        if (!cb_async_err) {
                            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %.*s",
                                "send_response_stream_cb()", cb_async_err.message().size(), cb_async_err.message().data());
                            return;
                        }
                        auto cb_async = (std::move(*cb_async_err.unwrap()));
                        manapi::async::run ([res = std::move(res), cb_async = std::move(cb_async)] () mutable
                            -> manapi::future<> {
                            if (http_v1_1_is_chunked_data(res.get())) {
                                co_await cb_async([cdata = res->connection_data()]
                                    (slice_view buffs, bool fin)
                                    -> manapi::future<ssize_t> {
                                    auto chunk_err = co_await send_http_v1_1_chunked_data(cdata, buffs, fin);
                                    if (!chunk_err) {
                                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %.*s", "send_response_stream_cb()",
                                            chunk_err.msg().size(), chunk_err.msg().data());
                                        co_return -1;
                                    }
                                    co_return static_cast<ssize_t>(buffs.size());
                                });
                            }
                            else {
                                co_await cb_async([cdata = res->connection_data()]
                                    (slice_view buffs, bool fin)
                                    -> manapi::future<ssize_t> {
                                    co_return co_await cdata->worker->fwrite(cdata->conn, buffs, fin);
                                });
                            }
                            res->connection_data()->cb->call(true);
                        });
                    }
                }
                catch (std::exception const &e) {
                    manapi_log_error("%s failed due to %s", "send_response_stream_cb()", e.what());
                }
            });
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "send_response_stream_cb()", e.what());
    }
}


void manapi::net::http::internal::send_response_async_cb(std::unique_ptr<response> res, response_features_t features) {
    if (features.compressor) {
        manapi_log_error("%s failed due to %s", "send_response_async_cb()", "Compression isn't supported");
        return;
    }

    if (features.replacers) {
        manapi_log_error("%s failed due to %s", "send_response_async_cb()", "Replacers isn't supported");
        return;
    }

    auto const cdata = res->connection_data();

    if (http_v1_1_is_chunked_data(res.get()))
        res->header(std::string{http::H_TRANSFER_ENCODING}, "chunked").unwrap();

    auto task = mask_response(res.get(), false);
    manapi::async::run<int> (std::move(task),
        [res = std::move(res)] (std::exception_ptr err, int *result) mutable
        -> void {
            try {
                if (err) {

                    return;
                }

                if (result && *result == ERR_OK) {
                    auto cb_async_err = res->callback_async();
                    if (!cb_async_err) {
                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %.*s", "send_response_async_cb()",
                            cb_async_err.message().size(), cb_async_err.message().data());
                        return;
                    }
                    auto cb_async = (std::move(*cb_async_err.unwrap()));
                    manapi::async::run ([res = std::move(res), cb_async = std::move(cb_async)] () mutable
                        -> manapi::future<> {
                        try {
                            auto cdata = res->connection_data();
                            auto buffer = manapi::async::current()->memory_fabric().slice(65536).unwrap();

                            bool finish = false;

                            if (http_v1_1_is_chunked_data(res.get())) {
                                while (!finish) {
                                    auto const rhs = co_await cb_async(buffer, finish);
                                    if (rhs < 0) {
                                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s", "send_response_async_cb()", "cb returned an invalid length");
                                        co_return;
                                    }
                                    auto res = buffer.subslice(0, static_cast<std::size_t>(rhs));
                                    res.unwrap();
                                    auto chunk_err = co_await send_http_v1_1_chunked_data(cdata, res.unwrap(), finish);
                                    if (!chunk_err) {
                                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %.*s", "send_response_async_cb()",
                                            chunk_err.msg().size(), chunk_err.msg().data());
                                        co_return;
                                    }
                                }
                            }
                            else {
                                std::size_t cursor = 0;
                                while (!finish) {
                                    auto rhs = co_await cb_async(buffer, finish);
                                    if (rhs < 0) {
                                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s", "send_response_async_cb()", "cb returned an invalid length");
                                        co_return;
                                    }

                                    auto res = buffer.subslice(0, static_cast<std::size_t>(rhs));
                                    res.unwrap();

                                    rhs = co_await cdata->worker->fwrite(cdata->conn,
                                        res.unwrap(), finish);

                                    if (rhs <= 0)
                                        co_return;

                                    cursor += static_cast<std::size_t>(rhs);

                                    if (cursor == buffer.size())
                                        cursor = 0;
                                }
                            }
                        }
                        catch (std::exception const &e) {
                            manapi_log_error("%s failed due to %s", "send_response_async_cb()", e.what());
                        }
                    });
                }
            }
            catch (std::exception const &e) {
                manapi_log_error("%s failed due to %s", "send_response_async_cb()", e.what());
            }
        });
}

manapi::future<int> manapi::net::http::internal::mask_response(response* res, bool finish) {
    auto const cdata = res->connection_data();
    auto const global = cdata->worker->wrk_global();
    return global->send_response(cdata->conn, global, cdata->worker.get(), res, finish);
}

void manapi::net::http::internal::handle_income_request(uq_handle_data_t cdata, int status) {
    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "Handle HTTP request on %.*s conn:%p",
         cdata->req_data->uri.size(), cdata->req_data->uri.data(), cdata->conn.get());
    if (status >= 200 && status < 300)
        return manapi__handle_income_request(std::move(cdata), status);

    manapi__send_error_response(std::move(cdata), status);
}

manapi::future<void> manapi::net::http::internal::send_file(std::unique_ptr<response> res, std::shared_ptr<fs::fstream> f, std::size_t size) {
    auto const cdata = res->connection_data();
    std::size_t constexpr block_size = manapi::object_pool::area_size() * 16;

    auto write_block = cdata->worker->bufferpool().slice(block_size).unwrap();
    auto read_block = cdata->worker->bufferpool().slice(block_size).unwrap();

    co_await manapi::async::parallel_wait ([&] (manapi::reference<manapi::async::parallel_t> parallel_st) -> manapi::future<> {
        std::size_t current = static_cast<std::size_t>(f->tellg());

        size += current;

        auto prun = async::parallel_run<ssize_t>::create(parallel_st).unwrap();

        ssize_t rhs;

        if ((rhs = co_await f->read(write_block.subslice(0, std::min(block_size, size - current)).unwrap())) <= 0) {
            co_return;
        }

        try {
            while (size > current) {
                /* add count of the chars which will be sent at this iterration */
                current += static_cast<std::size_t>(rhs);
                bool readsome = size > current;
                if (readsome) {
                    auto status = prun->run(f->read(read_block.subslice(0, std::min(block_size, size - current)).unwrap()));
                    if (!status) {
                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH,
                                         "%s failed due to %.*s", "send_file", status.msg().size(),
                                         status.msg().data());
                        break;
                    }
                }

                auto sv = write_block.subslice(0, static_cast<std::size_t>(rhs)).unwrap();

                assert(sv.size() == static_cast<std::size_t>(rhs));
                if ((co_await cdata->worker->fwrite(cdata->conn, sv, !readsome)) <= 0) {
                    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "send_file() %p failed due to %s", cdata->conn.get(),
                                     "fwrite() <= 0");
                    /* failed to send */
                    co_return;
                }

                if ((rhs = co_await prun->get_or(static_cast<ssize_t>(0))) <= 0) {
                    break;
                }

                std::swap(write_block, read_block);
            }


            cdata->cb->call(current >= size);
        }
        catch (std::exception const &e) {
            manapi_log_trace(debug::LOG_TRACE_MEDIUM, "send_file() %p failed due to %s", cdata->conn.get(), e.what());
        }
    });
    co_return;
}

manapi::future<void> manapi::net::http::internal::send_text(std::unique_ptr<response> res, std::string text) {
    const char *current = text.data();
    auto sent = text.size();
    auto cdata = res->connection_data();

    while (sent != 0) {
        const ssize_t result = co_await cdata->worker->write(cdata->conn, current, sent, true);

        if (result < 0) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %s", "send_text()",
                "write failed");
            co_return;
        }

        if (static_cast<std::size_t>(result) > sent) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %s", "send_text()",
                "write incorrect");
            co_return;
        }

        sent -= static_cast<std::size_t>(result);

        current = current + result;
    }

    cdata->cb->call(true);
}

manapi::future<> manapi::net::http::internal::send_slice(std::unique_ptr<response> res, manapi::slice sv) {
    auto cdata = res->connection_data();

    while (!sv.empty()) {
        const ssize_t result = co_await cdata->worker->write(cdata->conn, sv, true);

        if (result < 0) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %s", "send_slice()",
                "write failed");
            co_return;
        }

        if (static_cast<std::size_t>(result) > sv.size()) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %s", "send_slice()",
                "write incorrect");
            co_return;
        }

        sv.shift_add(static_cast<std::size_t>(result)).unwrap();
    }

    cdata->cb->call(true);
}

void manapi::net::http::internal::expect_header(uq_handle_data_t cdata) {
    const auto expect = cdata->req_data->headers.find(H_EXPECT);
    if (expect != cdata->req_data->headers.end()) {
        if (expect->second == "100-continue") {
            auto resp = std::make_unique<http::response>(cdata.release(), http::CONTINUE_100, nullptr);
            send_response(std::move(resp));
        }
        else {
            cdata->cb->call(false);
        }
    }
    else {
        cdata->cb->call(true);
    }
}