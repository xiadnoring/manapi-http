#include <cstring>

#include "worker/ManapiHttp1Interface.hpp"
#include "http/ManapiHttp1.hpp"
#include "ManapiString.hpp"

#define HTTP_ALL_SWITCH(namecb, ...) \
manapi::net::worker::wrk_interface_global_t * httpctx; \
switch (conn->version) { case manapi::net::http::versions::HTTP_v0_9: \
case manapi::net::http::versions::HTTP_v1_0: \
case manapi::net::http::versions::HTTP_v1_1: \
httpctx = static_cast<manapi::net::worker::wrk_http_ctx_global_t *> (global->data)->http1.get(); \
return httpctx->namecb(__VA_ARGS__); \
case manapi::net::http::versions::HTTP_v2: \
httpctx = static_cast<manapi::net::worker::wrk_http_ctx_global_t *> (global->data)->http2.get(); \
return httpctx->namecb(__VA_ARGS__); \
case manapi::net::http::versions::HTTP_v3: \
httpctx = static_cast<manapi::net::worker::wrk_http_ctx_global_t *> (global->data)->http3.get(); \
return httpctx->namecb(__VA_ARGS__); \
default:break; }

enum http_v1_flags {
    HTTP1_BODY_CHUNKED = 1
};

void default_wrk_http_all_accept (const manapi::net::worker::shared_conn & conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    HTTP_ALL_SWITCH (accept_cb, conn, flags, buffer, nsize, p, httpctx, w);
}

void default_wrk_http_all_init (const manapi::net::worker::shared_conn & conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    HTTP_ALL_SWITCH (init_cb, conn, httpctx, w);
}

void default_wrk_http_all_cleanup (manapi::net::worker::connection * conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    HTTP_ALL_SWITCH (cleanup_cb, conn, httpctx, w);
}

void default_wrk_http_all_custom_read(const manapi::net::worker::shared_conn & conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    HTTP_ALL_SWITCH (custom_read_cb, conn, flags, buffer, nsize, p, httpctx, w);
}

void default_wrk_http_all_flush_custom_read(const manapi::net::worker::shared_conn & conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    HTTP_ALL_SWITCH (flush_custom_read_cb, conn, httpctx, w);
}

bool default_wrk_http_all_update_limit_rate(const manapi::net::worker::shared_conn & conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    HTTP_ALL_SWITCH (update_limit_rate, conn, httpctx, w);
    return false;
}

void default_wrk_http_all_cleanup_global_cb (manapi::net::worker::wrk_interface_global_t *data, manapi::net::worker::base *w) {
    auto wrk_global = static_cast<manapi::net::worker::wrk_http_ctx_global_t *>(data->data);

    if (wrk_global->http1)
        wrk_global->http1->cleanup_global_cb(wrk_global->http1.get(), w);

    if (wrk_global->http2)
        wrk_global->http2->cleanup_global_cb(wrk_global->http2.get(), w);

    if (wrk_global->http3)
        wrk_global->http3->cleanup_global_cb(wrk_global->http3.get(), w);

    delete wrk_global;
    data->data = nullptr;
}

manapi::error::status manapi::net::worker::default_wrk_http_all_global_init(wrk_interface_global_t *global, worker::base *w) {
    if (global->data)
        return manapi::error::status_invalid_argument("global->data already exists");

    global->data = new manapi::net::worker::wrk_http_ctx_global_t{};
    global->accept_cb = default_wrk_http_all_accept;
    global->init_cb = default_wrk_http_all_init;
    global->cleanup_cb = default_wrk_http_all_cleanup;
    global->custom_read_cb = default_wrk_http_all_custom_read;
    global->flush_custom_read_cb = default_wrk_http_all_flush_custom_read;
    global->update_limit_rate = default_wrk_http_all_update_limit_rate;
    global->cleanup_global_cb = default_wrk_http_all_cleanup_global_cb;

    return manapi::error::status_ok();
}

manapi::error::status manapi::net::worker::default_wrk_http_all_global_add_version(wrk_interface_global_t *global, int version, std::unique_ptr<wrk_interface_global_t> http_t) {
    if (!global)
        return manapi::error::status_invalid_argument("global is null");


    auto data = static_cast<worker::wrk_http_ctx_global_t *> (global->data);

    switch(version) {
        case http::versions::HTTP_v0_9:
        case http::versions::HTTP_v1_0:
        case http::versions::HTTP_v1_1: {
            data->http1 = std::move(http_t);
            break;
        }
        case http::versions::HTTP_v2: {
            data->http2 = std::move(http_t);
            break;
        }
        case http::versions::HTTP_v3: {
            data->http3 = std::move(http_t);
            break;
        }
        default:
            return error::status_out_of_range("http version incorrect");
    }

    return error::status_ok();
}

void default_wrk_http1_custom_read (const manapi::net::worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto wrk_ctx = static_cast<manapi::net::worker::wrk_http1_ctx_t *> (conn->wrk.data);

    if (wrk_ctx->flgs & HTTP1_BODY_CHUNKED) {
        switch (auto rhs = manapi::net::http::http_v1_1_chunked_read(wrk_ctx->chunked_ctx.get(), w, conn, w->config(), buffer, nsize)) {
            case manapi::net::http::EHTTP_V1_1_CHUNKED_OK: {
                w->event_flags(conn, manapi::net::worker::base::CONN_RECV_END);
                w->feed_event(conn, manapi::net::worker::base::CONN_RECV_END, nullptr, 0, nullptr);
                break;
            }
            case manapi::net::http::EHTTP_V1_1_CHUNKED_READ: {
                break;
            }
            case manapi::net::http::EHTTP_V1_1_CHUNKED_ERR:
                default: {
                /* error */
                w->close_connection(conn, false);
                break;
            }
        }
    }
    else {
        assert(false && "not implemented");
    }
}

void default_wrk_http1_init (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    assert(!conn->wrk.data);

    auto tp = std::make_unique<manapi::net::worker::wrk_http1_ctx_t>();
    tp->ctx = std::make_unique<manapi::net::http::http_v1_1_t>();

    conn->version = manapi::net::http::versions::HTTP_v1_1;

    conn->wrk.data = tp.release();
}

void default_wrk_http1_cleanup (manapi::net::worker::connection* conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto wrk_data = static_cast<manapi::net::worker::wrk_http1_ctx_t *>(conn->wrk.data);
    delete wrk_data;
    conn->wrk.data = nullptr;
    conn->wrk.flags = 0;
}

void default_wrk_http1_global_cleanup (manapi::net::worker::wrk_interface_global_t *data, manapi::net::worker::base *w) {
    auto wrk_global = static_cast<manapi::net::worker::wrk_http1_ctx_global_t *>(data->data);
    delete wrk_global;
    data->data = nullptr;
}

void default_wrk_http1(const manapi::net::worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    if (flags & manapi::ev::DISCONNECT) {
        goto err;
    }

    try {
        auto wrk_data = static_cast<manapi::net::worker::wrk_http1_ctx_t *>(conn->wrk.data);

        if (flags & manapi::ev::READ) {
            int status;
            switch (auto const state = manapi::net::http::http_v1_1_work(
                wrk_data->ctx.get(), w->config(), &buffer, &nsize)) {
                case manapi::net::http::EHTTP_V1_1_PROTOCOL_PAYLOAD_TOO_LARGE:
                    status = manapi::net::http::PAYLOAD_TOO_LARGE_413;
                    goto exec;

                case manapi::net::http::EHTTP_V1_1_PROTOCOL_OK:
                    status = manapi::net::http::OK_200;
                    goto exec;

                case manapi::net::http::EHTTP_V1_1_PROTOCOL_UPGRADE: {
                    w->event_on(conn, std::unique_ptr<manapi::net::worker::worker_watcher_cb>(nullptr));
                    w->event_flags(conn, 0);

                    auto gctx = static_cast<manapi::net::worker::wrk_http1_ctx_global_t *> (global->data);
                    auto httpall = dynamic_cast<manapi::net::worker::interface_worker *>(w)->wrk_global();
                    auto httpallctx = static_cast<manapi::net::worker::wrk_http_ctx_global_t *> (httpall->data);

                    switch (wrk_data->ctx->http) {
                        case manapi::net::http::versions::HTTP_v0_9: {
                            goto err;
                        }
                        case manapi::net::http::versions::HTTP_v1_0: {
                            goto err;
                        }
                        case manapi::net::http::versions::HTTP_v1_1: {
                            goto err;
                        }
                        case manapi::net::http::versions::HTTP_v2: {
                            if (httpallctx->http2) {
                                global->cleanup_cb(conn.get(), global, w);
                                conn->version = manapi::net::http::versions::HTTP_v2;
                                httpallctx->http2->init_cb(conn, httpallctx->http2.get(), w);

                                break;
                            }
                            goto err;
                        }
                        case manapi::net::http::versions::HTTP_v3: {
                            /* i got you, bro */
                            goto err;
                        }
                        default: {
                            goto err;
                        }
                    }

                    if (nsize) {
                        w->feed_event(conn, manapi::net::worker::base::CONN_READ| manapi::net::worker::base::CONN_TOP_READ, buffer, nsize, p);

                        buffer += nsize;
                        nsize = 0;
                    }

                    break;
                }
                case manapi::net::http::EHTTP_V1_1_PROTOCOL_ERROR: {
                    goto err;
                }
                case manapi::net::http::EHTTP_V1_1_PROTOCOL_WANT_READ: {
                    /* skip */
                    break;
                }
                default: {
                    assert(false && "An invalid state for http_v1_1_work()");
                }
            }
            return;
exec:
            auto req_ptr = wrk_data->ctx->req.get();

            auto it_header = req_ptr->headers.find(manapi::net::http::HEADER.EXPECT);
            if (it_header != req_ptr->headers.end()) {
                ssize_t const copy = sizeof ("HTTP/1.1 100 Continue\r\n\r\n") - 1;
                auto const rhs = w->sync_write_ex (conn, static_cast<const char *>("HTTP/1.1 100 Continue\r\n\r\n"),
                    copy, true, 1e5);
                if (copy != rhs) {
                    goto err;
                }
            }

            w->waiting(conn, false);
            it_header = req_ptr->headers.find(manapi::net::http::HEADER.TRANSFER_ENCODING);
            if (it_header != req_ptr->headers.end()) {
                auto const values = manapi::net::http::parse_header_value(it_header->second);
                for (const auto &v : values) {
                    if (manapi::string::equals(v.value, "chunked", 0b11)) {
                        wrk_data->flgs |= HTTP1_BODY_CHUNKED;
                        conn->wrk.flags |= manapi::net::worker::WRK_INTERFACE_CUSTOM_READ;
                        wrk_data->chunked_ctx = std::make_unique<manapi::net::http::http_v1_1_chunked_t>();

                        continue;
                    }

                    goto err;
                }
            }

            auto cdata = std::make_unique<manapi::net::http::internal::handle_data_t>(conn,
                dynamic_cast<manapi::net::worker::interface_worker *>(w)->copy(), req_ptr, std::make_unique<manapi::net::http::internal::cont_callback_cb_t>(
                [w, conn, req = std::move(wrk_data->ctx->req)] (bool ok)
                -> void {
                    w->close_connection (conn, ok);
            }));

            w->event_on(conn, std::unique_ptr<manapi::net::worker::worker_watcher_cb>(nullptr));
            w->event_flags(conn, 0);

            if (nsize) {
                if (wrk_data->flgs & HTTP1_BODY_CHUNKED) {
                    default_wrk_http1_custom_read (conn, flags, buffer, nsize, p, global, w);
                }
                else {
                    w->feed_event(conn, manapi::net::worker::base::CONN_READ| manapi::net::worker::base::CONN_TOP_READ, buffer, nsize, p);
                }
                buffer += nsize;
                nsize = 0;
            }
            wrk_data->ctx.reset();


            cdata->router = w->site().handler(req_ptr);
            manapi::net::http::internal::handle_income_request(std::move(cdata), status);

            // char constexpr bb[] = "HTTP/1.1 200 OK\r\nContent-Length:0\r\nConnection:keep-alive\r\n\r\n";
            // ssize_t const copy = sizeof (bb) - 1;
            // auto const rhs = w->sync_write_ex (conn, bb,
            //     copy, true, 1e5);
            // if (copy != rhs) {
            //     goto err;
            // }
            // cdata->cb->call(true);
        }

        return;
    }
    catch (...) {
        /* fatal error */
    }

    err: w->close_connection(conn, false);
}

void default_wrk_http1_flush_read (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto wrk_ctx = static_cast<manapi::net::worker::wrk_http1_ctx_t *>(conn->wrk.data);
    if (wrk_ctx->flgs & HTTP1_BODY_CHUNKED) {
        switch (manapi::net::http::http_v1_1_chunked_flush(wrk_ctx->chunked_ctx.get(), w, conn)) {
            case manapi::net::http::EHTTP_V1_1_CHUNKED_OK: {
                w->event_flags(conn, manapi::net::worker::base::CONN_RECV_END);
                w->feed_event(conn, manapi::net::worker::base::CONN_RECV_END, nullptr, 0, nullptr);
                break;
            }
            case manapi::net::http::EHTTP_V1_1_CHUNKED_ERR: {
                w->close_connection(conn, false);
                break;
            }
            case manapi::net::http::EHTTP_V1_1_CHUNKED_READ: {
                break;
            }
            case manapi::net::http::EHTTP_V1_1_CHUNKED_WAIT: {
                break;
            }
        }
    }
    else {
        assert(false && "not implemented");
    }
}

manapi::error::status manapi::net::worker::default_wrk_http1_global_init (manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    if (global->data)
        return manapi::error::status_invalid_argument("global->data already exists");

    global->data = new manapi::net::worker::wrk_http1_ctx_global_t{};
    global->accept_cb = default_wrk_http1;
    global->cleanup_cb = default_wrk_http1_cleanup;
    global->cleanup_global_cb = default_wrk_http1_global_cleanup;
    global->custom_read_cb = default_wrk_http1_custom_read;
    global->flush_custom_read_cb = default_wrk_http1_flush_read;
    global->init_cb = default_wrk_http1_init;
    global->update_limit_rate = nullptr;

    return manapi::error::status_ok();
}