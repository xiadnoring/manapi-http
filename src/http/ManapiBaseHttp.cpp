#include <memory.h>
#include <fstream>
#include <memory>

#include "ManapiFetch.hpp"
#include "ManapiString.hpp"
#include "ManapiTime.hpp"
#include "http/ManapiHttpMime.hpp"
#include "http/ManapiBaseHttp.hpp"
#include "http/ManapiHttpRequest.hpp"
#include "http/ManapiHttpResponse.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "fs/ManapiFileStream.hpp"
#include "std/ManapiAsyncParallelRun.hpp"
#include "std/ManapiEasyCancellation.hpp"
#include "ext/ManapiMustache.hpp"
#include "crypto/ManapiCryptoUtils.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/ManapiSiteInternal.hpp"
#include "../include/ManapiDefaultErrors.hpp"
#include "../include/ManapiHttpStructs.hpp"

static const std::set<std::string> methods = {"POST", "GET", "HEAD", "OPTIONS", "TRACE", "PUT", "DELETE", "PATCH", "CONNECT"};

#ifdef MANAPIHTTP_FETCH_SUPPORT
struct response_proxy_data_t {
    ssize_t content_length;
    manapi::net::fetch fetch;
    std::unique_ptr<manapi::net::http::response> resp;
};
#endif

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
        manapi::error::status err;

        response_features_t features = {
            .compress = res->compress(),
            .compressor_for_file = nullptr,
            .compressor_for_string = nullptr,
            .replacers = std::move(res->replacers())
        };

        auto const cdata = res->connection_data();

        if (!features.compress.empty() && !res->partial_enabled()) {
            if (res->is_text()) {
                features.compressor_for_string = cdata->worker->site().compressor_for_string(features.compress);
            }
            else if (res->is_file()) {
                features.compressor_for_file = cdata->worker->site().compressor_for_file(features.compress);
            }

            if (features.compressor_for_file || features.compressor_for_string) {
                err = res->header(std::string{header::CONTENT_ENCODING}, features.compress);
                if (!err)
                    goto finish;
            }
        }


        // set time
        res->header(std::string{header::DATE}, std::format("{:%a, %d %b %Y %H:%M:%S} GMT", std::chrono::time_point_cast<std::chrono::seconds>(manapi::time::current_time(false).get_sys_time())));
        if (res->request_data()->http < versions::HTTP_v2) {
            auto const keepalive = cdata->worker->config()->keep_alive;
            if (keepalive) {
                err = res->header(std::string{header::CONNECTION}, std::string{header::KEEP_ALIVE});
            }
            else {
                err = res->header(std::string{header::CONNECTION}, "close");
            }
            if (!err)
                goto finish;
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
    std::string filepath;
    auto reserr = res->file();
    manapi::error::status err;
    if (!reserr)
        co_return;

    auto resfile = std::move(*reserr.unwrap());
    auto const cdata = res->connection_data();

    bool force_compress = false;

    while (true) {
        try {
            if (features.compressor_for_file) {
                if (features.replacers) {
                    manapi_log_error("send_response_file():replacers can not be using during compress");
                    co_return;
                }

                bool rst_compress = false;

                try {
                    auto rhs = co_await internal::compress_file(cdata->worker->site(), resfile, cdata->worker->site().config_cache_dir(), features.compress, features.compressor_for_file, force_compress);
                    if (!rhs.ok()) {
                        if (rhs.code() == manapi::ERR_UNAVAILABLE)
                            rst_compress = true;
                        else
                            rhs.unwrap();
                    }
                    else
                        filepath = rhs.unwrap();
                }
                catch (std::exception const &e) {
                    manapi_log_error("compress:Compress file failed due to %s", e.what());

                    rst_compress = true;
                }


                if (rst_compress) {
                    /* badness */
                    res->remove_header(http::header::CONTENT_ENCODING);
                    features.compressor_for_string = nullptr;
                    features.compressor_for_file = nullptr;
                    filepath = std::move(resfile);
                }
            }
            else {
                filepath = std::move(resfile);
            }

            auto status = filesystem::fstream::create (filepath);
            if (!status) {
                cdata->router = std::move(cdata->router->error);
                uq_handle_data_t uq_cdata (res->connection_data_release());
                send_error_response(std::move(uq_cdata), http::INTERNAL_SERVER_ERROR_500);
                co_return;
            }

            auto f = status.unwrap();
            auto fres = co_await f.open(ev::FS_O_RDONLY);

            if (!fres.ok()) {
                if (force_compress) {
                    /** no way */
                    cdata->router = std::move(cdata->router->error);
                    uq_handle_data_t uq_cdata (res->connection_data_release());
                    send_error_response(std::move(uq_cdata), http::INTERNAL_SERVER_ERROR_500);
                    co_return;
                }
                else {
                    force_compress = true;
                    MANAPIHTTP_LOG("Failed to open the file: {}", filepath);
                }
                continue;
            }

            // set headers

            std::string_view mimetype = mime::mime_by_file_path(
                resfile.empty() ? filepath : resfile);
            std::vector<replace_founded_item> replacers;

            if (mimetype.starts_with("text/")) {
                auto mimegen = stringify_header_value({{mimetype, {{"charset", "UTF-8"}}}});
                err = res->header(std::string{header::CONTENT_TYPE}, std::move(mimegen));
            }
            else {
                err = res->header(std::string{header::CONTENT_TYPE}, std::string{mimetype});
            }
            if (!err) {
                co_return;
            }

            // get file size
            auto stat_res = co_await manapi::filesystem::async_file_size(filepath);
            ssize_t fileSize = stat_res.unwrap();
            ssize_t dynamicFileSize = fileSize;

            // replacers
            if (features.replacers) {
                if (fileSize <= 65536) {
                    auto token = res->req()->cancellation().sub();
                    token.timeout(5000);
                    auto data = co_await manapi::filesystem::async_read(filepath, ev::FS_O_RDONLY, -1, std::move(token));
                    res->text(data.unwrap());
                    manapi::async::run(send_response_text(std::move(res), std::move(features)));
                    co_return;
                }

                manapi_log_error("file too large to use replacers. max size: %d", 65536);
            }



            // partial enabled
            if (res->partial_enabled() && res->contains_ranges() && res->config()->partial_data_min_size <= fileSize) {
                if (features.compressor_for_file) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s:%s failed due to %s", "send_response_file()", "compressor_for_file",
                        "compress with the partial content is not supported");
                }

                res->status(http::PARTIAL_CONTENT_206);
                res->header(std::string{header::ACCEPT_RANGES}, "bytes");

                ssize_t start = 0,
                        back = fileSize - 1,
                        size;

                auto ranges = res->ranges();
                ssize_t const ranges_size = ranges ? static_cast<ssize_t>(ranges->size()) : 0;

                switch (ranges_size) {
                    case 0:
                        break;
                    case 1: {
                        auto &range = ranges->operator[](0);
                        if (range.first != -1) {
                            start = range.first;
                        }

                        if (range.second != -1) {
                            back = range.second;
                        }
                        else {
                            back = fileSize - 1;
                        }
                        break;
                    }
                    default: {
                        manapi_log_error("%s: %s", "send_response_file()", "multi bytes not supported");
                    }
                }

                size = back - start + 1;

                res->header(std::string{header::CONTENT_LENGTH}, std::to_string(size));
                res->header(std::string{header::CONTENT_RANGE}, std::format("bytes {}-{}/{}", start, back, fileSize));

                auto task = mask_response(res.get(), size == 0);
                manapi::async::run<int>(std::move(task),
                    [size, start, f, res = std::move(res)] (std::exception_ptr err, int *value) mutable
                    -> void {
                        if (err) {
                            return;
                        }

                        if (value && *value == ERR_OK) {
                            // set start position
                            f.seekg(start);
                            // set size and send
                            if (size) {
                                manapi::async::run (send_file(std::move(res), f, size));
                            }
                        }
                });
            }
            else {
                res->header(std::string{header::CONTENT_LENGTH}, std::to_string(dynamicFileSize));

                if (fileSize) {
                    auto task = mask_response(res.get(), false);
                    manapi::async::run<int>(std::move(task),
                        [res = std::move(res), fileSize, replacers = std::move(replacers), f = std::move(f), cdata = std::move(cdata)] (std::exception_ptr err, int *value) mutable
                        -> void {
                            if (err) {
                                return;
                            }

                            if (value && *value == ERR_OK) {
                                if (replacers.empty()) {
                                    // without replacers
                                    manapi::async::run( send_file(std::move(res), f, fileSize));
                                }
                                else {
                                    manapi::async::run(send_file(std::move(res), f, fileSize));
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

        break;
    }
}

manapi::future<void> manapi::net::http::internal::send_response_text(std::unique_ptr<response> res, response_features_t features) {
    try {
        // may contains decoded / encoded body
        auto errtext = res->text();
        if (!errtext) {
            co_return;
        }
        std::string plaintext = std::move(*errtext.unwrap());

        if (features.replacers) {
            manapi::kainjow::mustache::data data;
            for (auto &v : *features.replacers) {
                data.set(v.first, v.second);
            }

            std::string input = std::move(plaintext);
            manapi::kainjow::mustache::mustache tmpl (input);
            plaintext = tmpl.render(data);
        }

        if (features.compressor_for_string) {
            // encode content !
            auto r = (*features.compressor_for_string)(plaintext);
            if (r.ok())
                plaintext = r.unwrap();
            else {
                features.compressor_for_string = nullptr;
                res->remove_header(header::CONTENT_ENCODING);
            }
        }

        res->header(std::string{header::CONTENT_LENGTH}, std::to_string(plaintext.size()));

        if (!res->headers().contains(header::CONTENT_TYPE)) {
            res->header(std::string{header::CONTENT_TYPE}, "text/html; charset=UTF-8");
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
                    if (plaintext.empty())
                        res->connection_data()->cb->call(true);
                    else
                        manapi::async::run (send_text(std::move(res), std::move(plaintext)));
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

        std::unique_ptr<response_proxy_data_t> proxy_data (new (std::nothrow) response_proxy_data_t{});
        if (!proxy_data)
            goto send_error;

        auto proxy_res = manapi::net::fetch::create(std::move(*proxy_err.unwrap()), res->req()->cancellation().sub());
        if (!proxy_res)
            goto send_error;

        proxy_data->fetch = proxy_res.unwrap();
        proxy_data->content_length = 0;
        proxy_data->resp = std::move(res);

        auto proxy_setup = std::move(proxy_data->resp->proxy_setup_cb());

        if (proxy_setup) {
            proxy_setup->operator()(proxy_data->fetch);
        }

        proxy_data->fetch.headers({{"ranges", "0-"}});

        proxy_data->fetch.handle_async_headers (
            [p = proxy_data.get()](std::map<std::string, std::string, std::less<>> headers) mutable
            -> manapi::future<bool> {
                try {
                    p->resp->status_code(p->fetch.status_code());

                    auto it = headers.find(header::CONTENT_LENGTH);
                    if (it != headers.end()) {
                        char *strend = nullptr;
                        p->content_length = std::strtoll(it->second.data(), &strend, 10);
                        if (!p->resp->header(std::string{header::CONTENT_LENGTH}, it->second))
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

        proxy_data->fetch.handle_async_body(
            [p = proxy_data.get()](slice_view buffs, bool fin) mutable
                -> manapi::future<ssize_t> {
            if (p->content_length > 0 && p->content_length <= buffs.size())
                fin = true;

            auto const cdata = p->resp->connection_data();
            auto rhs = co_await cdata->worker->fwrite (cdata->conn, buffs, fin);

            if (rhs < 0)
                co_return -1;

            p->content_length -= rhs;

            co_return rhs;
        }).unwrap();

        auto task = proxy_data->fetch.async_doit();
        manapi::async::run<error::status> (std::move(task), [proxy_data = std::move(proxy_data)]
                (std::exception_ptr err, manapi::error::status *status) mutable -> void {
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
    cdata->router = std::move(cdata->router->error);
    uq_handle_data_t uq_cdata (res->connection_data_release());
    send_error_response(std::move(uq_cdata), http::INTERNAL_SERVER_ERROR_500);
    co_return;
}

manapi::future<> manapi::net::http::internal::send_response_formdata(std::unique_ptr<response> res, response_features_t features) {
    try {
        auto formdata_err = res->formdata();
        if (!formdata_err) {
            co_return;
        }
        auto formdata = std::make_unique<formdata_send>(std::move(*formdata_err.unwrap()));

        if (features.compressor_for_file || features.compressor_for_string) {
            manapi_log_error("formdata: Compression isn't supported");
            co_return;
        }

        if (features.replacers) {
            manapi_log_error("formdata: Replacers isn't supported");
            co_return;
        }

        auto size_res = co_await formdata->payload_size();
        auto size = size_res.unwrap();

        auto task = mask_response(res.get(), false);
        manapi::async::run<int> (std::move(task),
            [size, formdata = std::move(formdata), res = std::move(res)] (std::exception_ptr err, int *result) mutable
            -> void {
                if (err) {
                    /* failed */
                    return;
                }


                if (result && *result == ERR_OK) {
                    auto boundary = formdata->generate_boundary();
                    size += formdata->multipart_size(static_cast<ssize_t>(boundary.size())).unwrap();

                    res->header(std::string{header::CONTENT_LENGTH}, std::to_string(size));
                    res->header(std::string{header::CONTENT_TYPE}, stringify_header_value({{"multipart/form-data", {{"boundary", boundary.substr(2)}}}}));

                    auto task = formdata->data2multipart(std::move(boundary),
                        res->config()->buffer_size,
                        [res = res.get()] (manapi::slice_view slice, bool fin)
                        -> future<manapi::error::status> {
                            auto const cdata = res->connection_data();

                            if (co_await cdata->worker->fwrite(cdata->conn, slice, fin) < 0) {
                                co_return error::status_aborted("worker:Failed to write");
                            }

                            co_return error::status_ok();
                        });

                    manapi::async::run<manapi::error::status>(std::move(task),
                        [res = std::move(res)] (std::exception_ptr err, manapi::error::status *status) mutable
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

bool http_v1_1_is_chunked_data (manapi::net::http::response *resp) {
    if (resp->connection_data()->conn->version == manapi::net::http::versions::HTTP_v1_1
        && !resp->headers().contains(manapi::net::http::header::CONTENT_LENGTH)) {
        return true;
    }
    return false;
}

manapi::future<manapi::error::status> send_http_v1_1_chunked_data (manapi::net::http::internal::handle_data_t *cdata, manapi::slice_view buffs, bool &finish) {
    try {
        auto const rhs = buffs.size();
        std::string_view const msg = {"0\r\n\r\n"};

        if (rhs) {
            char header[20];

            auto res = std::to_chars(header, header + sizeof (header), rhs, 16);

            if (res.ec != std::errc()) {
                auto errmsg = std::make_error_code(res.ec).message();
                manapi_log_trace("%s due to %.*s", "send_http_v1_1_chunked_data:to_chars failed", errmsg.size(), errmsg.data());
                co_return manapi::error::status_internal("send_http_v1_1_chunked_data:to_chars failed");
            }

            auto len = static_cast<ssize_t>(res.ptr - header);

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
                    static_cast<ssize_t>(msg.size()), true) <= 0)
                goto err;
        }
        co_return manapi::error::status_ok();
err:
        co_return manapi::error::status_internal("send_http_v1_1_chunked_data:Write failed");
    }
    catch (std::bad_alloc const &) {
        co_return manapi::error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "send_http_v1_1_chunked_data:Failed", e.what());
        co_return manapi::error::status_internal("send_http_v1_1_chunked_data:Failed");
    }
}

void manapi::net::http::internal::send_response_sync_cb(std::unique_ptr<response> res, response_features_t features) {
    if (features.compressor_for_file || features.compressor_for_string) {
        manapi_log_error("%s: %s", "send_response_sync_cb()", "Compression isn't supported");
        return;
    }

    if (features.replacers) {
        manapi_log_error("%s: %s", "send_response_sync_cb()", "Replacers isn't supported");
        return;
    }


    if (http_v1_1_is_chunked_data(res.get())) {
        auto err = res->header(std::string{http::header::TRANSFER_ENCODING}, "chunked");
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
                                    std::max<std::size_t>(res->config()->buffer_size, 64L));
                                auto buffer = bufres.unwrap();
                                while (!finish) {
                                    auto rhs = cb_sync(buffer.data(), buffer.size(), finish);
                                    slice_ref buffs;
                                    if (rhs)
                                        buffs.push_back(buffer.data(), rhs);
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
                                    ssize_t total = 0;
                                    for (auto it = slices.begin(); it != slices.end() && !finish; ) {
                                        auto rhs = cb_sync(static_cast<char *>(it.buffer()) + cursor,
                                            it.size() - cursor, finish);

                                        if (rhs < 0) {
                                            manapi_log_trace("send_response_sync_cb():The callback returned an invalid length");
                                            co_return;
                                        }

                                        cursor += rhs;

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
        if (features.compressor_for_file || features.compressor_for_string) {
            manapi_log_error("%s failed due to %s", "send_response_stream_cb()", "Compression isn't supported");
            return;
        }

        if (features.replacers) {
            manapi_log_error("%s failed due to %s", "send_response_stream_cb()", "Replacers isn't supported");
            return;
        }

        if (http_v1_1_is_chunked_data(res.get())) {
            auto err = res->header(std::string{http::header::TRANSFER_ENCODING}, "chunked");
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
                                    co_return buffs.size();
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
    if (features.compressor_for_file || features.compressor_for_string) {
        manapi_log_error("%s failed due to %s", "send_response_async_cb()", "Compression isn't supported");
        return;
    }

    if (features.replacers) {
        manapi_log_error("%s failed due to %s", "send_response_async_cb()", "Replacers isn't supported");
        return;
    }

    auto const cdata = res->connection_data();

    if (http_v1_1_is_chunked_data(res.get()))
        res->header(std::string{http::header::TRANSFER_ENCODING}, "chunked");

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
                                    auto res = buffer.subslice(0, rhs);
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

                                    auto res = buffer.subslice(0, rhs);
                                    res.unwrap();

                                    rhs = co_await cdata->worker->fwrite(cdata->conn,
                                        res.unwrap(), finish);

                                    if (rhs <= 0)
                                        co_return;

                                    cursor += rhs;

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

static int handle_request_stringify_ip (manapi::net::http::manapi_socket_information *inf, manapi::net::worker::base *w, manapi::net::worker::connection *conn) {
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

static manapi::error::status execute_user_callback (manapi::net::http::handler_template_t &handle, manapi::net::http::request *req,
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
                return manapi::error::status_resource_exhausted();

            manapi::net::http::uresponse uresp (resp, std::move(after_work_uq));

            try {
                err.unwrap()->operator()(*req, std::move(uresp));
            }
            catch (std::exception const &e) {
                manapi_log_trace("%s due to %s", "http:User cb failed", e.what());
            }
        }

        return manapi::error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s due to %s", "http:User cb failed", e.what());
        return manapi::error::status_internal("http:User cb failed");
    }
}

namespace manapi::net::http::internal {
    static void handle_income_request_err_ (std::unique_ptr<response> res, std::exception_ptr err) {
        uq_handle_data_t cdata (res->connection_data_release());
        char msg[256];
        std::size_t msg_size = sizeof (msg);
        manapi::extract_exception_ptr(std::move(err), nullptr, msg, &msg_size);
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s due to %.*s",
            "an error occurred while processing the HTTP request",
            msg_size, msg);
        cdata->router = std::move(cdata->router->error);
        send_error_response(std::move(cdata), http::SERVICE_UNAVAILABLE_503);
        return;
    }
    static void handle_income_request_next_ (handler_template_t *handler, std::unique_ptr<response> res, int index) {
        auto const cdata_ptr = res->connection_data();
        auto const res_ptr = res.get();

        auto &layer = cdata_ptr->router->layer;
        if (index >= 0 && index < layer.size()) {
            auto &layer_handler = layer[index]->handler;
            auto status = execute_user_callback (layer_handler, res_ptr->req(), res_ptr,
                [handler, res = std::move(res), index = index + 1] (std::exception_ptr err) mutable -> void {
                    if (err)
                        return handle_income_request_err_(std::move(res), std::move(err));

                    if (!res->req()->propagation())
                        // skip other layers and handlers
                        send_response(std::move(res));
                    else
                        handle_income_request_next_(handler, std::move(res), index);
            });

            return;
        }


        if (handler) {
            auto status = execute_user_callback (*handler, res_ptr->req(), res_ptr,
            [res = std::move(res)] (std::exception_ptr err) mutable -> void {
                    if (err)
                        return handle_income_request_err_(std::move(res), std::move(err));

                    try {
                        send_response(std::move(res));
                    }
                    catch (const std::exception &e) {
                        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %s",
                            "send_response", e.what());
                    }
            });
        }
        else {
            send_response (std::move(res));
        }
    }

    static void handle_income_request_(uq_handle_data_t cdata, int status) {
        try {
            // handler function not be found
            if (!cdata->router->handler) {
                // check exists static folder/file
                if (cdata->router->statics) {

                    // if statics exists
                    std::string path;


                    auto maxsize = static_cast<size_t> (cdata->req_data->divided >= 0
                        ? cdata->req_data->divided : cdata->req_data->path.size());

                    size_t path_reserved = 0;

                    for (size_t i = cdata->router->statics_parts_len; i < maxsize; i++) {
                        path_reserved += 1;
                        path_reserved += cdata->req_data->path[i].size();
                    }

                    path.reserve(path_reserved);

                    for (size_t i = cdata->router->statics_parts_len; i < maxsize; i++) {
                        path += manapi::filesystem::path::delimiter;
                        path += cdata->req_data->path[i];
                    }

                    path = manapi::filesystem::path::join(cdata->router->statics->folder, path);

                    manapi::async::run([status, cdata = std::move(cdata), path = std::move(path)] () mutable
                        -> manapi::future<> {
                        bool exists = true;
                        uint64_t st_mode = 0;

                        co_await manapi::filesystem::async_stat(path, [&st_mode, &exists] (ev::stat_t *stat)
                            -> void {
                            if (stat) {
                                st_mode = stat->st_mode;
                            }
                            else {
                                exists = false;
                            }
                        });

                        if (exists) {
                            if (st_mode & (ev::IFREG|ev::IFLNK)) {
                                const auto ext = manapi::filesystem::path::extension(path);
                                auto const mime = mime::mime_by_file_extension(ext);
                                bool const binary = mime::mime_partitial_data(mime);
                                auto client = std::make_unique<manapi_socket_information>();

                                if (handle_request_stringify_ip(client.get(), cdata->worker.get(), cdata->conn.get())) {
                                    /* error */
                                    manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_INTERNAL, "stringify_ip(): ip get failed");
                                    co_return;
                                }

                                manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "conn:%p is %s:%u",
                                       cdata->conn.get(), client->ip.data(), static_cast<uint32_t>(client->port));

                                auto const handler = (cdata->router->statics->layer
                                    && cdata->router->statics->layer->handler) ? &cdata->router->statics->layer->handler : nullptr;

                                cdata->req_data->handler = cdata->router->handler;

                                auto req = std::make_unique<http::request> (std::move(client), cdata->req_data, &cdata->conn, cdata->worker);
                                auto res = std::make_unique<http::response> (cdata.release(), status, cdata->worker->config(), std::move(req));

                                res->compress_enabled(!binary);
                                res->partial_enabled(binary);
                                res->file(path);

                                handle_income_request_next_ (handler, std::move(res), 0);
                            }

                            else if (st_mode & (ev::IFDIR|ev::IFCHR|ev::IFBLK|ev::IFSOCK)) {
                                if (cdata) {
                                    cdata->router = std::move(cdata->router->error);
                                    send_error_response(std::move(cdata), http::FORBIDDEN_403);
                                }
                                co_return;
                            }
                        }

                        if (cdata) {
                            cdata->router = std::move(cdata->router->error);
                            send_error_response(std::move(cdata), http::NOT_FOUND_404);
                        }
                    },
                    [] (std::exception_ptr err) mutable
                        -> void {
                        if (err) {
                            return;
                        }
                    });
                }
                else {
                    cdata->router = std::move(cdata->router->error);
                    send_error_response(std::move(cdata), http::NOT_FOUND_404);
                }
                return;
            }

            auto client = std::make_unique<manapi_socket_information>();

            if (handle_request_stringify_ip(client.get(),
                cdata->worker.get(), cdata->conn.get())) {
                /* error */
                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_INTERNAL, "stringify_ip(): ip get failed");
                return;
            }

            manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "conn:%p is %s:%u",
                   cdata->conn.get(), client->ip.data(), static_cast<uint32_t>(client->port));

            auto handler = &cdata->router->handler->handler;

            cdata->req_data->handler = cdata->router->handler;

            auto req = std::make_unique<http::request> (std::move(client), cdata->req_data, &cdata->conn, cdata->worker);
            auto res = std::make_unique<http::response> (cdata.release(), status, cdata->worker->config(), std::move(req));

            handle_income_request_next_ (handler, std::move(res), 0);

            return;
        }
        catch (const manapi::exception &e) {
            switch (e.err_num()) {
                case ERR_ABORTED:
                    return;
                default:
                    MANAPIHTTP_LOG("Unexpected error: {}", e.what());
            }
        }
        catch (const std::exception &e) {
            MANAPIHTTP_LOG("Unexpected error: {}", e.what());
        }

        cdata->router = std::move(cdata->router->error);
        send_error_response(std::move(cdata), SERVICE_UNAVAILABLE_503);
    }
}


void manapi::net::http::internal::handle_income_request(uq_handle_data_t cdata, int status) {
    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "Handle HTTP request on %.*s conn:%p",
         cdata->req_data->uri.size(), cdata->req_data->uri.data(), cdata->conn.get());
    if (status >= 200 && status < 300)
        return handle_income_request_(std::move(cdata), status);

    cdata->router = std::move(cdata->router->error);
    send_error_response(std::move(cdata), status);
}

void manapi::net::http::internal::send_error_response(uq_handle_data_t cdata, int status) {
    if (!cdata->router) {
        return;
    }

    assert((cdata));

    handle_income_request_(std::move(cdata), status);
}

manapi::future<void> manapi::net::http::internal::send_file(std::unique_ptr<response> res, filesystem::fstream f, ssize_t size) {
    ssize_t const block_size = 4096 * 16;
    auto const cdata = res->connection_data();

    auto write_block = cdata->worker->bufferpool().slice(block_size).unwrap();
    auto read_block = cdata->worker->bufferpool().slice(block_size).unwrap();

    ssize_t current = f.tellg();

    size += current;

    async::parallel_run<ssize_t> parallel;
    auto parallel_status = async::parallel_run<ssize_t>::create();
    if (!parallel_status)
        co_return;

    parallel = parallel_status.unwrap();

    ssize_t rhs;

    if ((rhs = co_await f.read(write_block.subslice(0,
        std::min(block_size, size - current)).unwrap())) <= 0) {
        co_return;
    }

    try {
        while (size > current) {
            /* add count of the chars which will be sent at this iterration */
            current += rhs;
            bool readsome = size > current;
            if (readsome) {
                auto status = parallel.run(f.read(read_block.subslice(0,
                    std::min(block_size, size - current)).unwrap()));
                if (!status) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH,
                        "%s failed due to %.*s", "send_file", status.msg().size(), status.msg().data());
                    break;
                }
            }

            auto sv = write_block.subslice(0, rhs).unwrap();

            assert(sv.size() == rhs);
            if ((co_await cdata->worker->fwrite (cdata->conn, sv, !readsome)) <= 0) {
                manapi_log_trace(debug::LOG_TRACE_MEDIUM, "send_file() %p failed due to %s", cdata->conn.get(), "fwrite() <= 0");
                /* failed to send */
                goto err;
            }

            if ((rhs = co_await parallel.get_or(0)) <= 0) {
                break;
            }

            std::swap(write_block, read_block);
        }


        cdata->cb->call(current >= size);

        co_return;
    }
    catch (std::exception const &e) {
        manapi_log_trace(debug::LOG_TRACE_MEDIUM, "send_file() %p failed due to %s", cdata->conn.get(), e.what());
    }

    err:
    if (parallel.some()) {
        co_await parallel.get_or(0);
    }
    co_return;
}

manapi::future<void> manapi::net::http::internal::send_text(std::unique_ptr<response> res, std::string text) {
    const char *current = text.data();
    auto sent = static_cast<ssize_t>(text.size());
    auto cdata = res->connection_data();

    while (sent != 0) {
        const ssize_t result = co_await cdata->worker->write(cdata->conn, current, sent, true);

        if (result <= 0) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %s", "send_text()",
                "write failed");
            co_return;
        }

        if (result > sent) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s failed due to %s", "send_text()",
                "write incorrect");
            co_return;
        }

        sent -= result;

        current = current + result;
    }

    cdata->cb->call(true);
}

void manapi::net::http::internal::expect_header(uq_handle_data_t cdata) {
    const auto expect = cdata->req_data->headers.find(header::EXPECT);
    if (expect != cdata->req_data->headers.end()) {
        if (expect->second == "100-continue") {
            auto resp = std::make_unique<http::response>(cdata.release(), http::CONTINUE_100, cdata->worker->config(), nullptr);
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

std::string generate_cache_name(const std::string &file, const std::string &ext) {
    std::string name = std::format("{}-{:%Y_%m_%d_%H_%M_%S}-{}.{}", manapi::filesystem::path::basename(std::forward<const std::string&> (file)),
        manapi::time::current_time (true), manapi::string::random(25), ext);

    return std::move(name);
}

manapi::future<manapi::error::status_or<std::string>> manapi::net::http::internal::compress_file(net::http::site site, std::string file, std::string folder, std::string compress, response_features_t::compress_file_cb *compressor, bool force_compress) {
    std::string filepath;
    std::string cached;

    try {
        std::chrono::time_point<std::chrono::system_clock> filetime;
        {
            auto ft_res = co_await manapi::filesystem::async_last_time_write(file);
            filetime = ft_res.unwrap();
        }
        // compressor
        if (force_compress) {
            cached.clear();
        }
        else {
            auto res = co_await site.get_compressed_cache_file(file, compress, filetime);
            if (res.ok())
                cached = res.unwrap();
            else {
                if (res.code() == ERR_NOT_FOUND)
                    cached.clear();
                else
                    co_return manapi::error::status_unavailable("busy");
            }
        }

        if (cached.empty()) {
            auto res = co_await site.set_locked_cache_file(file, true, compress);
            try {
                if (!res.ok()) {
                    res = manapi::error::status_unavailable("busy");
                    goto err;
                }

                try {
                    auto exists = co_await filesystem::async_exists (folder);
                    if (!exists.ok() || !exists.unwrap())
                        (co_await filesystem::async_mkdir(folder, ev::IRUSR|ev::IWUSR)).unwrap();
                }
                catch (std::exception const &e) {
                    manapi_log_error("%s due to %s", "mkdir cache directory failed", e.what());
                    res =  error::status_internal("mkdir cache directory failed");
                    goto err;
                }

                filepath = folder + generate_cache_name(file, compress);

                auto compress_res = co_await (*compressor)(file, filepath);
                if (!compress_res.ok()) {
                    res = std::move(compress_res);
                    goto err;
                }

                res = co_await site.set_compressed_cache_file(file, filepath, compress, filetime);
                if (!res.ok()) {
                    res = manapi::error::status_unavailable("busy");
                    goto err;
                }

                co_return std::move(filepath);
            }
            catch (std::exception const &e) {
                manapi_log_error("%s failed due to %s", "file compress", e.what());
                res = manapi::error::status_internal("file compress");
            }

err:
            if (!res) {
                manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s failed due to %.*s",
                    "file compress", res.msg().size(), res.msg().data());
            }

            res = co_await site.set_locked_cache_file(file, false, compress);

            if (!res.ok())
                manapi_log_error("compress:Failed to unlock compressed file due to %s:%s", res.status_msg(), res.msg());

            co_return manapi::error::status_unavailable("busy");
        }
        else {
            filepath = std::move(cached);
        }

        co_return std::move(filepath);
    }
    catch (std::exception const &e) {
        manapi_log_error("compress:Compress file filed due to %s", e.what());
    }

    co_return error::status_internal("compress:Something gets wrong");
}