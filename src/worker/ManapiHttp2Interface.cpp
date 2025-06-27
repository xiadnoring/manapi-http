#include "worker/ManapiHttp2Interface.hpp"
#include "worker/ManapiHttp2Worker.hpp"
#include "http/ManapiHttp2.hpp"
#include "worker/ManapiHttp1Interface.hpp"
#include "ManapiHttpResponse.hpp"

extern manapi::net::worker::http_v2_callbacks_t default_wrk_http2_callbacks;

int default_wrk_http2_cleanup (manapi::net::worker::connection *conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto wrk_data = static_cast<manapi::net::worker::wrk_http2_ctx_t *>(conn->wrk.data);
    delete wrk_data;
    conn->wrk.flags = 0;
    conn->wrk.data = nullptr;
    return 0;
}

int default_wrk_http2(const manapi::net::worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto wrk_ctx = static_cast<manapi::net::worker::wrk_http2_ctx_t *> (conn->wrk.data);
    auto http_v2_ctx = wrk_ctx->ctx.get();

    int rhs;

    if (flags & manapi::ev::DISCONNECT) {
        goto err;
    }

    if (flags & manapi::ev::WRITE) {
        if (manapi::net::http::http_v2_on_write(http_v2_ctx)) {
            goto err;
        }
    }

    if (flags & manapi::ev::READ) {
        auto const config = w->config();
        while (true) {
            rhs = manapi::net::http::http_v2_work(http_v2_ctx, config, &buffer, &nsize);
            switch (rhs) {
                case manapi::net::http::EHTTP_V2_PROTOCOL_OK: {
                    manapi::net::http::http_v2_on_close (http_v2_ctx);
                    if (http_v2_ctx->streams->empty()) {
                        http_v2_ctx->conn = nullptr;
                        w->close_connection(conn, true);
                    }
                    break;
                }
                case manapi::net::http::EHTTP_V2_PROTOCOL_WANT_READ: {
                    break;
                }
                case manapi::net::http::EHTTP_V2_PROTOCOL_ERROR: {
                    goto err;
                }
                case manapi::net::http::EHTTP_V2_IO_ERROR: {
                    goto err;
                }
                case manapi::net::http::EHTTP_V2_NEW_STREAM: {
                    /**
                     * stream id always must be at the end
                     * of the map (ctx->streams).
                     **/
                    auto const s = http_v2_ctx->streams->rbegin();
                    if (s == http_v2_ctx->streams->rend())
                        continue;

                    w->waiting(conn, false);
                    s->second->version = manapi::net::http::versions::HTTP_v2;
                    auto const globalctx = static_cast<manapi::net::worker::wrk_http2_ctx_global_t *> (global->data);

                    manapi::async::current()->etaskpool()->append_task(
                        [conn, status = http_v2_ctx->status,  id = s->first, w, w2 = globalctx->worker] () -> void {
                            auto wrk_ctx = static_cast<manapi::net::worker::wrk_http2_ctx_t *> (conn->wrk.data);
                            if (!wrk_ctx)
                                return;
                            auto http_v2_ctx = wrk_ctx->ctx.get();
                            auto s = http_v2_ctx->streams->find(id);
                            if (s == http_v2_ctx->streams->end())
                                return;

                            auto const sdata = s->second->as<manapi::net::http::http_v2_stream_t>();
                            auto const req_ptr = sdata->req.get();
                            assert(req_ptr);
                            std::cout << s->first<<" " << req_ptr->uri  << "\n";

                            auto cdata = std::make_unique<manapi::net::http::internal::handle_data_t>(s->second, w2,
                                req_ptr, std::make_unique<manapi::net::http::internal::cont_callback_cb_t>(
                                [w, sconn = s->second, conn, req = std::move(sdata->req)] (bool ok) mutable
                                -> void {
                                    manapi::async::current()->etaskpool()->append_task(
                                        [w = std::move(w), ok, conn = std::move(conn), sconn = std::move(sconn)] () -> void {
                                            auto const sdata = sconn->as<manapi::net::http::http_v2_stream_t>();
                                            auto ctx = static_cast<manapi::net::worker::wrk_http2_ctx_t *>(conn->wrk.data);

                                            ctx->gctx->worker->close_connection(sconn, ok);

                                            manapi::net::http::http_v2_on_close_stream(ctx->ctx.get(), sdata->id);

                                            if (ctx->ctx->streams->empty()) {
                                                if (ctx->ctx->current == -1 ||
                                                    (ctx->ctx->flags & manapi::net::http::HTTP2_CTX_FLAG_WANT_CLOSE)) {
                                                    if (manapi::net::http::http_v2_on_close (ctx->ctx.get())) {
                                                        /* error */
                                                    }
                                                    ctx->ctx->conn = nullptr;
                                                    w->close_connection(conn, true);
                                                }
                                                else {
                                                    w->waiting(conn, true);
                                                }
                                            }
                                    });
                            }));

                            // this->event_on(conn, std::unique_ptr<worker_watcher_cb>(nullptr));
                            // this->event_flags(conn, 0);

                            cdata->router = w->site().handler(req_ptr);
                            manapi::net::http::internal::handle_income_request(std::move(cdata), status);
                    });


                    continue;
                }
                default: {
                    goto err;
                }
            }

            break;
        }
    }

    return 0;
    err: {
        if (manapi::net::http::http_v2_on_close (http_v2_ctx)) {
            /* error */
        }
        if (http_v2_ctx->streams->empty()) {
            w->waiting(conn, true);
            http_v2_ctx->conn = nullptr;
            w->close_connection(conn, false);
        }
    }
    return 0;
}

int default_wrk_http2_init (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    try {
        assert(!conn->wrk.data);

        auto tp = std::make_unique<manapi::net::worker::wrk_http2_ctx_t>();
        tp->ctx = std::make_unique<manapi::net::http::http_v2_t>();

        conn->wrk.data = tp.release();

        conn->wrk.flags |= manapi::net::worker::WRK_INTERFACE_CUSTOM_RATE_LIMIT;

        auto wrk_ctx = static_cast<manapi::net::worker::wrk_http2_ctx_t *> (conn->wrk.data);
        auto global_ctx = static_cast<manapi::net::worker::wrk_http2_ctx_global_t *> (global->data);

        wrk_ctx->ctx->conn = conn;
        wrk_ctx->ctx->worker = w;
        wrk_ctx->ctx->http_v2_worker = global_ctx->worker.get();
        wrk_ctx->gctx = global_ctx;

        w->event_on(conn, std::make_unique<manapi::net::worker::worker_watcher_cb>(
            [w, global]
            (const manapi::net::worker::shared_conn & conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p)
            -> void {
            default_wrk_http2(conn, flags, buffer, nsize, p, global, w);
        }));

        w->event_flags(conn, manapi::ev::READ|manapi::ev::WRITE);
        return 0;
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("default http2 init failed due to {}", e.what());
    }
    return manapi::ERR_UNKNOWN;
}

void default_wrk_http2_update_limit_rate (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::wrk_http2_ctx_t *> (conn->wrk.data);
    if (http_v2_ctx->ctx->streams) {
        for (const auto &s : *http_v2_ctx->ctx->streams) {
            static_cast<manapi::net::worker::wrk_http2_ctx_global_t *> (global->data)
                ->worker->update_limit_rate_stream(s.second);
        }
    }
}

int default_wrk_http2_global_cleanup (manapi::net::worker::wrk_interface_global_t *data, manapi::net::worker::base *w) {
    auto ctx = static_cast<manapi::net::worker::wrk_http2_ctx_global_t *> (data->data);
    delete ctx;
    data->data = nullptr;
    return 0;
}

manapi::future<ssize_t> default_wrk_http2_send_response (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global,
    manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) {
    co_return co_await manapi::net::http::http_v2_response(w, conn, res->status_code(), std::move(res->headers()), finish);
}

manapi::error::status manapi::net::worker::default_wrk_http2_global_init (manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    assert(!global->data);

    auto tp = std::make_unique<wrk_http2_ctx_global_t>();

    tp->worker = std::make_shared<worker::http_v2>(w, &default_wrk_http2_callbacks);
    tp->worker->init();

    global->data = tp.release();

    global->accept_cb = default_wrk_http2;
    global->init_cb = default_wrk_http2_init;
    global->cleanup_cb = default_wrk_http2_cleanup;
    global->cleanup_global_cb = default_wrk_http2_global_cleanup;
    global->flush_custom_read_cb = nullptr;
    global->update_limit_rate = default_wrk_http2_update_limit_rate;
    global->custom_read_cb = nullptr;
    global->send_response = default_wrk_http2_send_response;

    return error::status_ok();
}

bool default_wrk_http2_is_writable (const manapi::net::worker::shared_conn &conn) {
    auto s = conn->as<manapi::net::http::http_v2_stream_t>();
    if (s->flags & manapi::net::http::HTTP2_STREAM_PRIORITY_LOCKED)
        return false;
    if ((s->ctx->flags & manapi::net::http::HTTP2_CTX_FLAG_BLOCK_WRITE))
        return false;

    return true;
}

int default_wrk_http2_want_write (const manapi::net::worker::shared_conn &conn) {
    auto s = conn->as<manapi::net::http::http_v2_stream_t>();
    s->ctx->worker->event_toggle(s->ctx->conn, manapi::ev::WRITE, true);
    return 0;
}

manapi::net::worker::connection::ipdata_t * default_wrk_http2_ipdata (manapi::net::worker::connection *conn) {
    auto s = conn->as<manapi::net::http::http_v2_stream_t>();
    return s->ctx->worker->ipdata(s->ctx->conn.get());
}

manapi::net::worker::http_v2_callbacks_t default_wrk_http2_callbacks {
    .http_v2_write = manapi::net::http::http_v2_write,
    .http_v2_on_read_stream = manapi::net::http::http_v2_on_read_stream,
    .http_v2_want_write = default_wrk_http2_want_write,
    .http_v2_rst_stream = manapi::net::http::http_v2_rst_stream,
    .http_v2_is_writable = default_wrk_http2_is_writable,
    .http_v2_ip_data = default_wrk_http2_ipdata
};