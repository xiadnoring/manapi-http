#include <memory.h>
#include <fstream>
#include <memory>

#include "http/base_http.hpp"
#include "../include/ManapiHttpStructs.hpp"
#include "services/ManapiFetch.hpp"
#include "ManapiFilesystem.hpp"
#include "ManapiHash.hpp"
#include "ManapiHttpMime.hpp"
#include "ManapiString.hpp"
#include "ManapiTime.hpp"
#include "async/ManapiAsyncFileStream.hpp"
#include "async/ManapiAsyncParallelRun.hpp"

#include "ManapiHttpRequest.hpp"
#include "ManapiHttpResponse.hpp"
#include "../include/ManapiDefaultErrors.hpp"
#include "crypto/ManapiCryptoUtils.hpp"

static const std::set<std::string> methods = {"POST", "GET", "HEAD", "OPTIONS", "TRACE", "PUT", "DELETE", "PATCH", "CONNECT"};

void manapi::net::http::internal::send_response(uq_handle_data_t cdata, std::unique_ptr<response> res) {
    std::string response;
    std::string compressed;

    response_features_t features = {
        .compress = res->compress(),
        .compressor_for_file = nullptr,
        .compressor_for_string = nullptr,
        .replacers = std::move(res->replacers())
    };

    if (!features.compress.empty() && !res->partial_enabled()) {
        if (res->is_text()) {
            features.compressor_for_string = &cdata->worker->site().compressor_for_string(features.compress);
        }
        else if (res->is_file()) {
            features.compressor_for_file = &cdata->worker->site().compressor_for_file(features.compress);
        }

        if (features.compressor_for_file || features.compressor_for_string) {
            res->header(HEADER.CONTENT_ENCODING, features.compress);
        }
    }


    // set time
    res->header(HEADER.DATE, std::format("{:%a, %d %b %Y %H:%M:%S} GMT", std::chrono::time_point_cast<std::chrono::seconds>(manapi::time::current_time(false).get_sys_time())));
    if (res->request_data()->http < versions::HTTP_v2) {
        auto const keepalive = cdata->worker->config()->keep_alive;
        if (keepalive) {
            res->header(HEADER.CONNECTION, HEADER.KEEP_ALIVE);
        }
        else {
            res->header(HEADER.CONNECTION, "close");
        }
    }

    switch (res->data_type()) {
        case internal::RESPONSE_FILE:
            manapi::async::run(send_response_file(std::move(cdata), std::move(res), std::move(features)));
        break;
        case internal::RESPONSE_TEXT:
            manapi::async::run(send_response_text(std::move(cdata), std::move(res), std::move(features)));
        break;
        case internal::RESPONSE_PROXY:
            manapi::async::run(send_response_proxy(std::move(cdata), std::move(res), std::move(features)));
        break;
        case internal::RESPONSE_FORMDATA:
            manapi::async::run(send_response_formdata(std::move(cdata), std::move(res), std::move(features)));
        break;
        case internal::RESPONSE_ASYNC_CALLBACK:
            send_response_async_cb(std::move(cdata), std::move(res), std::move(features));
        break;
        case internal::RESPONSE_SYNC_CALLBACK:
            send_response_sync_cb(std::move(cdata), std::move(res), std::move(features));
        break;
        case internal::RESPONSE_STREAM:
            send_response_stream_cb(std::move(cdata), std::move(res), std::move(features));
        break;
        default: {
            auto const cdataptr = cdata.get();
            manapi::async::run<ssize_t>(mask_response(cdataptr, res.get(), true),
                [cdata = std::move(cdata), res = std::move(res)] (std::exception_ptr err, ssize_t *result)
                -> void {
                    if (err) {
                        /* failed */
                        return;
                    }
                    cdata->cb->call(true);
            });
        }
    }
}

manapi::future<void> manapi::net::http::internal::send_response_file(uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features) {
    std::string filepath;
    auto resfile = std::move(res->file());

    bool force_compress = false;

    while (true) {
        if (features.compressor_for_file) {
            if (features.replacers) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_FAILED_PRECONDITION, "replacers can not be using during compress");
            }

            try {
                filepath = co_await internal::compress_file(cdata->worker->site(), resfile, cdata->worker->site().config_cache_dir(), features.compress, features.compressor_for_file, force_compress);
            }
            catch (std::exception const &e) {
                manapi::async::current()->logger()
                    ->debug(manapi::logger::default_service, "compress file failed due to {}", e.what());

                /* badness */
                res->remove_header(http::HEADER.CONTENT_ENCODING);
                features.compressor_for_string = nullptr;
                features.compressor_for_file = nullptr;
                filepath = std::move(resfile);
            }
        }
        else {
            filepath = std::move(resfile);
        }

        filesystem::fstream f (filepath);
        auto fres = co_await f.open(ev::FS_O_RDONLY);

        if (!fres.ok()) {
            if (force_compress) {
                /** no way */
                cdata->router = std::move(cdata->router->error);
                send_error_response(std::move(cdata), http::INTERNAL_SERVER_ERROR_500);
                co_return;
            }
            else {
                force_compress = true;
                MANAPIHTTP_LOG("Failed to open the file: {}", filepath);
            }
            continue;
        }

        try {
            // set headers

            std::string mimetype = mime::mime_by_file_path(
                resfile.empty() ? filepath : resfile);
            std::vector<replace_founded_item> replacers;

            if (mimetype.size() > sizeof ("text")) {
                if (strncmp("text", mimetype.data(), sizeof ("text") - 1) == 0) {
                    mimetype = stringify_header_value({{mimetype, {{"charset", "UTF-8"}}}});
                }
            }

            res->header(HEADER.CONTENT_TYPE, mimetype);

            // get file size
            ssize_t fileSize = co_await manapi::filesystem::async_file_size(filepath);
            ssize_t dynamicFileSize = fileSize;

            // replacers
            if (features.replacers) {
                replacers = co_await found_replacers_in_file(manapi::async::current(), filepath, 0, fileSize, *features.replacers);

                for (const auto &replacer: replacers) {
                    dynamicFileSize = static_cast<ssize_t> (
                        dynamicFileSize - (replacer.pos.second - replacer.pos.first + 1) + replacer.value->size());
                }
            }

            // partial enabled
            if (res->partial_enabled() && res->config()->partial_data_min_size <= fileSize) {
                if (features.compressor_for_file) {
                    THROW_MANAPIHTTP_EXCEPTION(ERR_FAILED_PRECONDITION,
                                           "the compress '{}' with the partial content is not supported.",
                                           features.compress);
                }


                if (features.replacers) {
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_FAILED_PRECONDITION, "replacers can not be use with partial");
                }

                res->status(http::PARTIAL_CONTENT_206);
                res->header(HEADER.ACCEPT_RANGES, "bytes");

                ssize_t start = 0,
                        back = fileSize - 1,
                        size;

                auto ranges = res->ranges();
                ssize_t const ranges_size = ranges ? static_cast<ssize_t>(ranges->size()) : 0;

                switch (ranges_size) {
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
                    case 0:
                        break;

                    default:
                        THROW_MANAPIHTTP_EXCEPTION2(ERR_FAILED_PRECONDITION, "multi bytes not supported");
                }

                size = back - start + 1;

                res->header(HEADER.CONTENT_LENGTH, std::to_string(size));
                res->header(HEADER.CONTENT_RANGE, std::format("bytes {}-{}/{}", start, back, fileSize));

                auto task = mask_response(cdata.get(), res.get(), size == 0);
                manapi::async::run<ssize_t>(std::move(task),
                    [size, start, f, cdata = std::move(cdata), res = std::move(res)] (std::exception_ptr err, ssize_t *value) mutable
                    -> void {
                        if (err) {
                            return;
                        }

                        if (value && *value >= 0) {
                            // set start position
                            f.seekg(start);
                            // set size and send
                            if (size) {
                                manapi::async::run (send_file(std::move(cdata), f, size));
                            }
                        }
                });
            }
            else {
                res->header(HEADER.CONTENT_LENGTH, std::to_string(dynamicFileSize));

                if (fileSize) {
                    auto task = mask_response(cdata.get(), res.get(), false);
                    manapi::async::run<ssize_t>(std::move(task),
                        [res = std::move(res), fileSize, replacers = std::move(replacers), f = std::move(f), cdata = std::move(cdata)] (std::exception_ptr err, ssize_t *value) mutable
                        -> void {
                            if (err) {
                                return;
                            }

                            if (value && *value >= 0) {
                                if (replacers.empty()) {
                                    // without replacers
                                    manapi::async::run( send_file(std::move(cdata), f, fileSize));
                                }
                                else {
                                    manapi::async::run(send_file(std::move(cdata), f, fileSize));
                                }
                            }
                    });
                }
                else {
                    auto task = mask_response(cdata.get(), res.get(), true);
                    manapi::async::run<ssize_t>(std::move(task), [cdata = std::move(cdata), res = std::move(res)] (std::exception_ptr err, ssize_t *result)
                        -> void { if (err) { return; } cdata->cb->call(true); });
                }
            }
        }
        catch (std::exception const &e) {
            MANAPIHTTP_LOG("send response failed due to {}", e.what());
        }

        break;
    }
}

manapi::future<void> manapi::net::http::internal::send_response_text(uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features) {
    // may contains decoded / encoded body
    std::string plaintext = std::move(res->text());

    if (features.compressor_for_string) {
        // encode content !
        auto r = (*features.compressor_for_string)(plaintext);
        plaintext = std::move(r);
    }

    res->header(HEADER.CONTENT_LENGTH, std::to_string(plaintext.size()));

    if (!res->headers().contains(HEADER.CONTENT_TYPE)) {
        res->header(HEADER.CONTENT_TYPE, "text/html; charset=UTF-8");
    }

    auto task = mask_response(cdata.get(), res.get(), plaintext.empty());
    manapi::async::run<ssize_t>(std::move(task),
        [plaintext = std::move(plaintext), cdata = std::move(cdata), res = std::move(res)] (std::exception_ptr err, ssize_t *value) mutable
        -> void {
            if (err) {
                /* failed */
                return;
            }

            if (value && *value >= 0) {
                /* ok */
                manapi::async::run (send_text(std::move(cdata), std::move(plaintext)));
            }
    });

    co_return;
}

manapi::future<void> manapi::net::http::internal::send_response_proxy(uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features) {
#ifdef MANAPIHTTP_FETCH_SUPPORT
    auto proxy = std::make_unique<fetch>(std::move(res->url()));
    auto proxy_setup = std::move(res->proxy_setup_cb());

    if (proxy_setup) {
        proxy_setup->operator()(*proxy);
    }

    proxy->headers({{"ranges", "0-"}});

    auto content_length = std::make_unique<ssize_t>(0);

    proxy->handle_async_headers (
        [&content_length = *content_length.get(), proxy = proxy.get(), cdata = cdata.get(), res = res.get()](std::map<std::string, std::string> headers) mutable
        -> manapi::future<bool> {
        res->status_code(proxy->status_code());

        auto it = headers.find(HEADER.CONTENT_LENGTH);
        if (it != headers.end()) {
            content_length = std::stoll(it->second);
            res->header(HEADER.CONTENT_LENGTH, it->second);
        }

        const auto rhs1 = co_await mask_response(cdata, res, content_length == 0);
        if (rhs1 < 0) {
            co_return false;
        }

        co_return true;
    });

    proxy->handle_async_body(
        [cdata = cdata.get(), &content_length = *content_length.get()](char *buffer, ssize_t size) mutable
            -> manapi::future<ssize_t> {
        auto rhs = co_await cdata->worker->fwrite (cdata->conn, buffer,
            size, content_length <= size);

        if (rhs < 0)
            co_return -1;

        content_length -= rhs;
        co_return rhs;
    });

    auto task = proxy->async_doit();
    manapi::async::run (std::move(task), [proxy = std::move(proxy),
        content_length = std::move(content_length),  cdata = std::move(cdata), res = std::move(res)]
            (std::exception_ptr err) mutable -> void {
            if (err) {
                /* failed */
                std::string msg;
                manapi::extract_exception_ptr(std::move(err), nullptr, &msg);
                std::cerr << msg << "\n";
                return;
            }


            cdata->cb->call(true);
        });

    co_return;
#else
    THROW_MANAPIHTTP_EXCEPTION2(ERR_INTERNAL, "Fetch is required");
#endif
}

manapi::future<> manapi::net::http::internal::send_response_formdata(uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features) {
    auto formdata = std::make_unique<formdata_send>(std::move(res->formdata()));

    if (features.compressor_for_file || features.compressor_for_string) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_FAILED_PRECONDITION, "formdata: Compression isn't supported");
    }

    if (features.replacers) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_FAILED_PRECONDITION, "formdata: Replacers isn't supported");
    }

    auto size = co_await formdata->payload_size();

    auto task = mask_response(cdata.get(), res.get(), false);
    manapi::async::run<ssize_t> (std::move(task),
        [size, formdata = std::move(formdata), cdata = std::move(cdata), res = std::move(res)] (std::exception_ptr err, ssize_t *result) mutable
        -> void {
            if (err) {
                /* failed */
                return;
            }

            if (result && *result >= 0) {
                auto boundary = formdata->generate_boundary();
                size += formdata->multipart_size(static_cast<ssize_t>(boundary.size()));

                res->header(HEADER.CONTENT_LENGTH, std::to_string(size));
                res->header(HEADER.CONTENT_TYPE, stringify_header_value({{"multipart/form-data", {{"boundary", boundary.substr(2)}}}}));

                auto task = formdata->data2multipart(std::move(boundary),
                    res->config()->buffer_size,
                    [cdata = cdata.get(), res = res.get()] (const void *buffer, ssize_t size)
                    -> future<> {
                        /**
                         * Invalid param: finish. IT MUST NOT be always false
                         * FINISH !!
                         */
                        if (co_await cdata->worker->fwrite(cdata->conn, buffer, size, false) < 0) {
                            THROW_MANAPIHTTP_EXCEPTION2(ERR_ABORTED, "failed to write");
                        }
                    });

                manapi::async::run(std::move(task),
                    [res = std::move(res), cdata = std::move(cdata)] (std::exception_ptr err) mutable
                    -> void {
                        if (err) {
                            return;
                        }

                        cdata->cb->call(true);
                });
            }
        });

    co_return;
}

bool http_v1_1_is_chunked_data (manapi::net::http::internal::uq_handle_data_t &cdata, manapi::net::http::response *resp) {
    if (cdata->conn->version == manapi::net::http::versions::HTTP_v1_1
        && !resp->headers().contains(manapi::net::http::HEADER.CONTENT_LENGTH)) {
        return true;
    }
    return false;
}

manapi::future<> send_http_v1_1_chunked_data (manapi::net::http::internal::uq_handle_data_t &cdata, ssize_t rhs, const void *buffer, std::size_t size, bool &finish) {
    if (rhs < 0)
        THROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_INVALID_ARGUMENT, "The callback returned an invalid length");

    std::string_view const msg = {"0\r\n\r\n"};

    if (rhs) {
        char header[20];

        auto res = std::to_chars(header, header + sizeof (header), rhs, 16);

        if (res.ec != std::errc())
            THROW_MANAPIHTTP_EXCEPTION(manapi::ERR_INTERNAL, "to_chars failed: {}", std::make_error_code(res.ec).message());

        auto len = static_cast<ssize_t>(res.ptr - header);

        if (len + 2 > sizeof (header) - 1)
            /* TODO: think */
            goto err;

        memcpy (header + len, static_cast<const char *>("\r\n"), 2);
        len += 2;

        if (co_await cdata->worker->fwrite(cdata->conn, header, len, false) <= 0)
            goto err;

        if (co_await cdata->worker->fwrite(cdata->conn, buffer, rhs, false) <= 0)
            goto err;

        if (co_await cdata->worker->fwrite(cdata->conn, msg.data() + 1, 2, false) <= 0)
            goto err;
    }

    if (finish) {
        if (co_await cdata->worker->fwrite(cdata->conn, msg.data(),
                static_cast<ssize_t>(msg.size()), rhs == 0) <= 0)
            goto err;
    }
    co_return;
err:
    THROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_ABORTED, "write(...) failed");
}

void manapi::net::http::internal::send_response_sync_cb(uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features) {
    if (features.compressor_for_file || features.compressor_for_string) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_FAILED_PRECONDITION, "Compression isn't supported");
    }

    if (features.replacers) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_FAILED_PRECONDITION, "Replacers isn't supported");
    }


    if (http_v1_1_is_chunked_data(cdata, res.get()))
        res->header(http::HEADER.TRANSFER_ENCODING, "chunked");

    auto task = mask_response(cdata.get(), res.get(), false);
    manapi::async::run<ssize_t> ( std::move(task),
        [res = std::move(res), cdata = std::move(cdata)] (std::exception_ptr err, ssize_t *result) mutable
        -> void {
            if (err) {

                return;
            }

            if (result && *result >= 0) {
                auto cb_sync = std::make_unique<http::response::resp_callback_sync>(std::move(res->callback_sync()));
                manapi::async::run ([res = std::move(res), cb_sync = std::move(cb_sync), cdata = std::move(cdata)] () mutable
                    -> manapi::future<> {

                        bool finish = false;
                        std::size_t cursor = 0;

                        if (http_v1_1_is_chunked_data(cdata, res.get())) {
                            auto buffer = manapi::async::current()->memory_fabric().buffer (
                                std::max(res->config()->buffer_size, 64L));
                            while (!finish) {
                                auto rhs = cb_sync->operator()(buffer.data(), buffer.size(), finish);
                                co_await send_http_v1_1_chunked_data(cdata, rhs, buffer.data(), buffer.size(), finish);
                            }
                        }
                        else {
                            auto slices = cdata->worker->bufferpool().slice(4096 * 16);
                            while (!finish) {
                                ssize_t total = 0;
                                for (auto it = slices.begin(); it != slices.end() && !finish; ) {
                                    auto rhs = cb_sync->operator()(static_cast<char *>(it.buffer()) + cursor,
                                        it.size() - cursor, finish);

                                    if (rhs < 0)
                                        THROW_MANAPIHTTP_EXCEPTION2(ERR_INVALID_ARGUMENT, "The callback returned an invalid length");

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

                                auto const rhs = co_await cdata->worker->fwrite(
                                    cdata->conn, slices.subslice(0, total).value(), finish);

                                if (rhs <= 0)
                                    co_return;
                            }
                        }
                    });
            }
        });
}

void manapi::net::http::internal::send_response_stream_cb(uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features) {
    if (features.compressor_for_file || features.compressor_for_string) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_FAILED_PRECONDITION, "Compression isn't supported");
    }

    if (features.replacers) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_FAILED_PRECONDITION, "Replacers isn't supported");
    }

    if (http_v1_1_is_chunked_data(cdata, res.get()))
        res->header(http::HEADER.TRANSFER_ENCODING, "chunked");

    auto task = mask_response(cdata.get(), res.get(), false);
    manapi::async::run<ssize_t> (std::move(task),
        [res = std::move(res), cdata = std::move(cdata)] (std::exception_ptr err, ssize_t *result) mutable
        -> void {
            if (err) {

                return;
            }

            if (result && *result >= 0) {
                auto cb_async = std::make_unique<http::response::resp_stream>(std::move(res->callback_stream()));
                manapi::async::run ([res = std::move(res), cb_async = std::move(cb_async), cdata = std::move(cdata)] () mutable
                    -> manapi::future<> {
                        if (http_v1_1_is_chunked_data(cdata, res.get())) {
                            co_await cb_async->operator()([&cdata]
                                (const void *buffer, ssize_t size, bool fin)
                                -> manapi::future<ssize_t> {
                                co_await send_http_v1_1_chunked_data(cdata, size, buffer, size, fin);
                                co_return size;
                            });
                        }
                        else {
                            co_await cb_async->operator()([&cdata]
                                (const void *buffer, ssize_t size, bool fin)
                                -> manapi::future<ssize_t> {
                                co_return co_await cdata->worker->fwrite(cdata->conn, buffer, size, fin);
                            });
                        }
                        cdata->cb->call(true);
                    });
            }
        });
}


void manapi::net::http::internal::send_response_async_cb(uq_handle_data_t cdata, std::unique_ptr<response> res, response_features_t features) {
    if (features.compressor_for_file || features.compressor_for_string) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_FAILED_PRECONDITION, "Compression isn't supported");
    }

    if (features.replacers) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_FAILED_PRECONDITION, "Replacers isn't supported");
    }

    if (http_v1_1_is_chunked_data(cdata, res.get()))
        res->header(http::HEADER.TRANSFER_ENCODING, "chunked");

    auto task = mask_response(cdata.get(), res.get(), false);
    manapi::async::run<ssize_t> (std::move(task),
        [res = std::move(res), cdata = std::move(cdata)] (std::exception_ptr err, ssize_t *result) mutable
        -> void {
            if (err) {

                return;
            }

            if (result && *result >= 0) {
                auto cb_async = std::make_unique<http::response::resp_callback_async>(std::move(res->callback_async()));
                manapi::async::run ([res = std::move(res), cb_async = std::move(cb_async), cdata = std::move(cdata)] () mutable
                    -> manapi::future<> {
                        auto buffer = manapi::async::current()->memory_fabric().buffer(
                            std::max(res->config()->buffer_size, 64L));

                        bool finish = false;

                        if (http_v1_1_is_chunked_data(cdata, res.get())) {
                            while (!finish) {
                                auto rhs = co_await cb_async->operator()(buffer.data(), buffer.size(), finish);
                                co_await send_http_v1_1_chunked_data(cdata, rhs, buffer.data(), buffer.size(), finish);
                            }
                        }
                        else {
                            std::size_t cursor = 0;
                            while (!finish) {
                                auto rhs = co_await cb_async->operator()(buffer.data() + cursor, buffer.size() - cursor, finish);
                                if (rhs < 0)
                                    THROW_MANAPIHTTP_EXCEPTION2(ERR_INVALID_ARGUMENT, "The callback returned an invalid length");

                                rhs = co_await cdata->worker->write(cdata->conn, buffer.data() + cursor, rhs, finish);
                                if (rhs <= 0)
                                    co_return;

                                cursor += rhs;

                                if (cursor == buffer.size())
                                    cursor = 0;
                            }
                        }
                    });
            }
        });
}

manapi::future<ssize_t> manapi::net::http::internal::mask_response(handle_data_t *cdata, response* res, bool finish) {
    co_return co_await cdata->worker->response(cdata->conn, res, finish);
}

int handle_request_stringify_ip (manapi::net::http::manapi_socket_information *inf, manapi::net::worker::base *w, manapi::net::worker::connection *conn) {
    auto ipdata = w->ipdata(conn);
    auto const sa = reinterpret_cast <struct sockaddr_in *> (ipdata->client.data);
    std::string buffer;
    int size;

    if (sa->sin_family == manapi::ev::IPv4) {
        size = sizeof ("xxx:xxx:xxx:xxx");
        buffer.resize(size);

        if (!inet_ntop(AF_INET, &sa->sin_addr, buffer.data(), size)) {
            return -1;
        }

        while (--size >= 0 && buffer[size] == '\0') {
            /* skip null bytes */
        }

        buffer.resize(size + 1);

        inf->ip = std::move(buffer);
        inf->port = htons(reinterpret_cast<struct sockaddr_in *> (&ipdata->client)->sin_port);
        return 0;
    }

    if (sa->sin_family == manapi::ev::IPv6) {
        size = sizeof ("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx");
        buffer.resize(size);

        if (!inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6 *>(sa)->sin6_addr, buffer.data(), size)) {
            return -1;
        }

        while (--size >= 0 && buffer[size] == '\0') {
            /* skip null bytes */
        }

        buffer.resize(size + 1);

        inf->ip = std::move(buffer);
        inf->port = htons(reinterpret_cast<struct sockaddr_in6 *> (&ipdata->client)->sin6_port);
        return 0;
    }

    return -1;
}

void manapi::net::http::internal::handle_income_request(uq_handle_data_t cdata, int status) {
    try {
        // handler function not be found
        if (!cdata->router->handler) {
            // check exists static folder/file
            if (cdata->router->statics) {
                // if statics exists
                std::string path;

                auto maxsize = static_cast<size_t> (cdata->req_data->divided >= 0
                    ? cdata->req_data->divided : cdata->req_data->path.size());

                for (size_t i = cdata->router->statics_parts_len; i < maxsize; i++) {
                    path += manapi::filesystem::path::delimiter + cdata->req_data->path[i];
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
                        if (st_mode & ev::IFREG) {
                            const auto ext = manapi::filesystem::path::extension(path);
                            auto mime = mime::mime_by_extension.find(ext);
                            bool binary = false;
                            if (mime != mime::mime_by_extension.end()) {
                                binary = mime::mime_partitial_data(mime->second);
                            }

                            auto client = std::make_unique<manapi_socket_information>();

                            if (handle_request_stringify_ip(client.get(), cdata->worker.get(), cdata->conn.get())) {
                                /* error */
                                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_INTERNAL, "stringify_ip(): ip get failed");
                                co_return;
                            }

                            auto req = std::make_unique<http::request> (std::move(client), cdata->req_data, &cdata->conn, cdata->worker, cdata->router->handler);
                            auto res = std::make_unique<http::response> (cdata->req_data, status, cdata->worker->config(), std::move(req));

                            // handle layers
                            for (auto &layer: cdata->router->layer) {
                                co_await layer->handler(*res->req(), *res);

                                if (!req->propagation()) {
                                    // skip other layers and handlers
                                    break;
                                }
                            }

                            res->compress_enabled(!binary);
                            res->partial_enabled(binary);
                            res->file(path);

                            if (cdata->router->statics->layer
                                && cdata->router->statics->layer->handler) {
                                co_await cdata->router->statics->layer->handler (*res->req(), *res);
                            }

                            try {
                                send_response(std::move(cdata), std::move(res));
                                co_return;
                            }
                            catch (const std::exception &e) {
                                MANAPIHTTP_LOG("Unexpected error: {}", e.what());
                            }
                        }

                        if (st_mode & ev::IFDIR) {

                        }

                        cdata->router = std::move(cdata->router->error);
                        send_error_response(std::move(cdata), http::FORBIDDEN_403);

                        co_return;
                    }

                    cdata->router = std::move(cdata->router->error);
                    send_error_response(std::move(cdata), http::NOT_FOUND_404);
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
        }

        auto req = std::make_unique<http::request> (std::move(client), cdata->req_data, &cdata->conn, cdata->worker, cdata->router->handler);
        auto res = std::make_unique<http::response> (cdata->req_data, status, cdata->worker->config(), std::move(req));

        auto task = [res = res.get(), data = cdata->router.get()] () -> future<> {

            // handle layers
            for (const auto &layer: data->layer) {
                co_await layer->handler(*res->req(), *res);

                if (!res->req()->propagation()) {
                    // skip other layers and handlers
                    break;
                }
            }

            co_await data->handler->handler(*res->req(), *res);
        };

        manapi::async::run( std::move(task),
            [res = std::move(res), cdata = std::move(cdata)] (std::exception_ptr err) mutable
                -> void {
                if (err) {
                    std::string msg;
                    manapi::extract_exception_ptr(std::move(err), nullptr, &msg);
                    manapi::async::current()->logger()->error(manapi::logger::default_service,
                        manapi::ERR_INTERNAL, "an error occurred while processing the HTTP request due to {}", msg);
                    cdata->router = std::move(cdata->router->error);
                    send_error_response(std::move(cdata), http::SERVICE_UNAVAILABLE_503);
                    return;
                }

                internal::send_response(std::move(cdata), std::move(res));
            });
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
    send_error_response(std::move(cdata), http::SERVICE_UNAVAILABLE_503);
}

void manapi::net::http::internal::send_error_response(uq_handle_data_t cdata, int status) {
    if (!cdata->router) {
        // TODO: Default error page
        return;
    }

    assert((cdata));

    handle_income_request(std::move(cdata), status);
}

manapi::future<void> manapi::net::http::internal::send_file(uq_handle_data_t cdata, filesystem::fstream f, ssize_t size) {
    ssize_t const block_size = 4096 * 16;

    auto write_block = cdata->worker->bufferpool().slice(block_size);
    auto read_block = cdata->worker->bufferpool().slice(block_size);

    ssize_t current = f.tellg();

    size += current;

    async::parallel_run<ssize_t> parallel;

    ssize_t rhs;
    std::exception_ptr error{nullptr};

    manapi::filesystem::fstream ff ("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
    (co_await ff.open(ev::FS_O_RDONLY)).throw_it();

    if ((rhs = co_await f.read(write_block.subslice(0,
        std::min(block_size, size - current)).value())) <= 0) {
        co_return;
    }

    try {
        while (size > current) {
            /* add count of the chars which will be sent at this iterration */
            current += rhs;
            bool readsome = size > current;
            if (readsome) {
                parallel.run(f.read(read_block.subslice(0,
                    std::min(block_size, size - current)).value()));
            }

            auto sv = write_block.subslice(0, rhs).value();
            // for (auto it = sv.begin(); it != sv.end(); it++) {
            //     if ((co_await cdata->worker->fwrite (cdata->conn, it.buffer(), it.size(), !readsome && it.is_last())) <= 0)
            //         /* failed to send */
            //         goto err;
            // }


            if ((co_await cdata->worker->fwrite (cdata->conn, sv, !readsome)) <= 0)
                /* failed to send */
                    goto err;

            if ((rhs = co_await parallel.get_or(0)) <= 0) {
                break;
            }

            std::swap(write_block, read_block);

            //printf("%s STEP: %zi LEFT: %zi NEED: %zi CURRENT: %zi\n", res.get_file().data(), sent, left, size, current);
        }


        cdata->cb->call(current >= size);

        co_return;
    }
    catch (...) {
        error = std::current_exception();
    }

    err: co_await parallel.get_or(0);

    if (error) {
        std::rethrow_exception(error);
    }
}

manapi::future<void> manapi::net::http::internal::send_text(uq_handle_data_t cdata, std::string text) {
    const char *current = text.data();
    auto sent = static_cast<ssize_t>(text.size());

    while (sent != 0) {
        const ssize_t result = co_await cdata->worker->write(cdata->conn, current, sent, true);

        if (result <= 0) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_INVALID_ARGUMENT, "Could not send the text: mask_write(...) = {}. Size: {}",
                                   result, sent);
        }

        if (result > sent) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_INVALID_ARGUMENT, "Total sent size > prepared sent size. {} > {}", result,
                                   sent);
        }

        sent -= result;

        current = current + result;
    }

    cdata->cb->call(true);
}

void manapi::net::http::internal::expect_header(uq_handle_data_t cdata) {
    const auto expect = cdata->req_data->headers.find(HEADER.EXPECT);
    if (expect != cdata->req_data->headers.end()) {
        if (expect->second == "100-continue") {
            auto resp = std::make_unique<http::response>(cdata->req_data, http::CONTINUE_100, cdata->worker->config(), nullptr);
            send_response(std::move(cdata), std::move(resp));
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

manapi::future<std::string> manapi::net::http::internal::compress_file(net::http::site site, std::string file, std::string folder, std::string compress, std::move_only_function<future<void>(std::string src, std::string dest)> *compressor, bool force_compress) {
    std::string filepath;
    std::pair<int, std::string> cached;

    auto filetime = co_await manapi::filesystem::async_last_time_write(file);
    // compressor
    if (force_compress) {
        cached.first = 2; /* force */
    }
    else {
        cached = co_await site.get_compressed_cache_file(file, compress, filetime);
    }

    if (cached.first) {
        try {
            try {
                if (!co_await filesystem::async_exists (folder))
                    co_await filesystem::async_mkdir(folder, ev::IRUSR|ev::IWUSR);
            }
            catch (std::exception const &e) {
                THROW_MANAPIHTTP_EXCEPTION (ERR_FILESYSTEM_FAILED, "mkdir cache directory failed due to {}", e.what());
            }
            filepath = folder + generate_cache_name(file, compress);

            co_await (*compressor)(file, filepath);

            co_await site.set_compressed_cache_file(file, filepath, compress, filetime);
        }
        catch (std::exception const &e) {
            manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_INTERNAL, "file compress failed due to {}", e.what());
            co_return file;
        }
    }
    else {
        filepath = std::move(cached.second);
    }

    co_return std::move(filepath);
}