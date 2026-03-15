#include "http/ManapiHttpResponse.hpp"
#include "worker/ManapiHttp2Worker.hpp"
#include "../include/http/ManapiHttp2.hpp"
#include "../include/http/ManapiHttp2Interface.hpp"
#include "../include/http/ManapiHttp1Interface.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/ManapiSiteInternal.hpp"

extern manapi::net::worker::http_v2_callbacks_t default_wrk_http2_callbacks;

int default_wrk_http2_cleanup (manapi::net::worker::connection *conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto wrk_data = static_cast<manapi::net::worker::wrk_http2_ctx_t *>(conn->wrk.data);
    delete wrk_data;
    conn->wrk.flags = 0;
    conn->wrk.data = nullptr;
    return 0;
}

static void wrk_close_conn2 ( manapi::net::worker::shared_conn conn, manapi::net::worker::shared_conn sconn, manapi::net::worker::base *w, bool ok) MANAPIHTTP_NOEXCEPT {
    //MANAPIHTTP_MUST_ALLOC_START
    // manapi::async::current()->etaskpool()->append_static_task(
    //     [w, ok, conn = std::move(conn), sconn = std::move(sconn)] () -> void {
            auto const sdata = sconn->as<manapi::net::http::http_v2_stream_t>();
            auto ctx = static_cast<manapi::net::worker::wrk_http2_ctx_t *>(conn->wrk.data);

            // worker is http2 worker
            ctx->gctx->worker->close_connection(sconn, ok ? manapi::net::worker::CLOSE_CONN_SHUTDOWN : manapi::net::worker::CLOSE_CONN_ERR);

            manapi::net::http::http_v2_on_close_stream(ctx->ctx.get(), sdata->id);

            if (!ctx->ctx->streams_size) {
                w->waiting(conn, true);
                if (ctx->ctx->current == -1 ||
                    (ctx->ctx->flags & manapi::net::http::HTTP2_CTX_FLAG_WANT_CLOSE)) {
                    if (manapi::net::http::http_v2_on_close (ctx->ctx.get())) {
                        /* error */
                    }
                    ctx->flgs |= manapi::net::http::HTTP2_CTX_FLAG_REALY_CLOSE;
                    ctx->ctx->conn = nullptr;
                    w->close_connection(conn, manapi::net::worker::CLOSE_CONN_FINISHED);
                }
            }
    // });
    // MANAPIHTTP_MUST_ALLOC_END
}

int default_wrk_http2(const manapi::net::worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto wrk_ctx = static_cast<manapi::net::worker::wrk_http2_ctx_t *> (conn->wrk.data);
    auto http_v2_ctx = wrk_ctx->ctx.get();

    int rhs;

    if (flags & manapi::net::worker::base::CONN_WANT_CLOSE) {
        rhs = manapi::net::http::http_v2_on_closing (http_v2_ctx);
        if (!rhs) {
            w->event_toggle(conn, false, manapi::net::worker::base::CONN_WANT_CLOSE);
        }
    }

    if (flags & manapi::ev::DISCONNECT) {
        goto err;
    }

    try {
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
                        if (!http_v2_ctx->streams_size) {
                            http_v2_ctx->flags |= manapi::net::http::HTTP2_CTX_FLAG_WANT_CLOSE| manapi::net::http::HTTP2_CTX_FLAG_REALY_CLOSE;
                            w->waiting(conn, true);
                            http_v2_ctx->conn = nullptr;
                            w->close_connection(conn, manapi::net::worker::CLOSE_CONN_FINISHED);
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
                        auto status = http_v2_ctx->status;
                        auto const s = http_v2_ctx->streams.rbegin();
                        if (s == http_v2_ctx->streams.rend() || !s->second)
                            continue;

                        w->waiting(conn, false);
                        s->second->version = manapi::net::http::versions::HTTP_v2;
                        auto const globalctx = static_cast<manapi::net::worker::wrk_http2_ctx_global_t *> (global->data);

                        try {
                            auto const sdata = s->second->as<manapi::net::http::http_v2_stream_t>();

                            // manapi::async::current()->etaskpool()->append_task(
                            //     [conn, status, id = s->first, w, w2 = globalctx->worker] () -> void {
                                    // auto wrk_ctx = static_cast<manapi::net::worker::wrk_http2_ctx_t *> (conn->wrk.data);
                                    // if (!wrk_ctx)
                                    //     return;

                                    // auto http_v2_ctx = wrk_ctx->ctx.get();
                                    // auto s = http_v2_ctx->streams.find(id);
                                    // if (s == http_v2_ctx->streams.end() || !s->second)
                                    //     return;

                                    // auto const sdata = s->second->as<manapi::net::http::http_v2_stream_t>();
                                    auto const req_ptr = sdata->req.get();
                                    assert(req_ptr);

                                    manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "http2: %d stream on %.*s", s->first, req_ptr->uri.size(), req_ptr->uri.data());

                                    try {
                                        auto cdata = std::make_unique<manapi::net::http::internal::handle_data_t>(s->second, globalctx->worker,
                                            req_ptr, std::make_unique<manapi::net::http::internal::cont_callback_cb_t>(
                                            [w, sconn = s->second, conn] (bool ok) mutable
                                            -> void {
                                                wrk_close_conn2 (conn, sconn, w, ok);
                                        }));

                                        // this->event_on(conn, std::unique_ptr<worker_watcher_cb>(nullptr));
                                        // this->event_flags(conn, 0);

                                        cdata->router = w->site().handler(req_ptr);
                                        manapi::net::http::internal::handle_income_request(std::move(cdata), status);
                                    }
                                    catch (std::exception const &e) {
                                        manapi_log_error("%s:%s failed due to %s", "http2", "new conn", e.what());
                                        wrk_close_conn2(conn, s->second, w, false);
                                    }
                            //});

                            sdata->flags |= manapi::net::http::HTTP2_STREAM_STARTED;
                        }
                        catch (std::exception const  &e ) {
                            manapi_log_error("%s:%s failed due to %s", "http2", "new conn", e.what());
                            wrk_close_conn2(conn, s->second, w, false);
                        }


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
    }
    catch (std::bad_alloc const &) {
        return manapi::ERR_RESOURCE_EXHAUSTED;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "default_wrk_http2", e.what());
    }

    err: try {
        if (manapi::net::http::http_v2_on_close (http_v2_ctx)) {
            /* error */
        }
        if (!http_v2_ctx->streams_size) {
            w->waiting(conn, true);
            http_v2_ctx->flags |= manapi::net::http::HTTP2_CTX_FLAG_WANT_CLOSE|manapi::net::http::HTTP2_CTX_FLAG_REALY_CLOSE;
            http_v2_ctx->conn = nullptr;
            w->close_connection(conn, manapi::net::worker::CLOSE_CONN_FINISHED);
        }
    }
    catch (std::bad_alloc const &) {
        return manapi::ERR_RESOURCE_EXHAUSTED;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "default_wrk_http2", e.what());
    }

    return 0;
}

int default_wrk_http2_init (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
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

        w->event_on(conn,
            [w, global]
            (const manapi::net::worker::shared_conn & conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p)
            -> void {
            default_wrk_http2(conn, flags, buffer, nsize, p, global, w);
        });

        w->event_flags(conn, manapi::ev::READ|manapi::ev::WRITE| manapi::net::worker::base::CONN_WANT_CLOSE);
        return 0;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "default_wrk_http2_init", e.what());
    }

    return manapi::ERR_UNKNOWN;
}

void default_wrk_http2_update_limit_rate (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto const http_v2_ctx = static_cast<manapi::net::worker::wrk_http2_ctx_t *> (conn->wrk.data);
    try {
        for (auto it = http_v2_ctx->ctx->streams.begin(); it != http_v2_ctx->ctx->streams.end(); ) {
            if (!it->second) {
                it = http_v2_ctx->ctx->streams.erase(it);
            }
            else {
                static_cast<manapi::net::worker::wrk_http2_ctx_global_t *> (global->data)
                    ->worker->update_limit_rate_stream(it->second);
                ++it;
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "default_wrk_http2_update_limit_rate", e.what());
    }
}

int default_wrk_http2_global_cleanup (manapi::net::worker::wrk_interface_global_t *data, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto ctx = static_cast<manapi::net::worker::wrk_http2_ctx_global_t *> (data->data);
    delete ctx;
    data->data = nullptr;
    return 0;
}

manapi::future<int> default_wrk_http2_send_response (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global,
    manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) {
    co_return co_await manapi::net::http::http_v2_response(w, conn, res->status_code(), std::move(res->headers()), finish);
}

manapi::status manapi::net::worker::default_wrk_http2_global_init (manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::interface_worker *w) {
    assert(!global->data);

    auto tp = std::make_unique<wrk_http2_ctx_global_t>();

    tp->worker = std::make_shared<worker::http_v2>(w, &default_wrk_http2_callbacks);
    // tp->worker->init(0);

    global->data = tp.release();

    global->accept_cb = default_wrk_http2;
    global->init_cb = default_wrk_http2_init;
    global->cleanup_cb = default_wrk_http2_cleanup;
    global->cleanup_global_cb = default_wrk_http2_global_cleanup;
    global->flush_custom_read_cb = nullptr;
    global->update_limit_rate = default_wrk_http2_update_limit_rate;
    global->custom_read_cb = nullptr;
    global->send_response = default_wrk_http2_send_response;

    return status_ok();
}

bool default_wrk_http2_is_writable (const manapi::net::worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto s = conn->as<manapi::net::http::http_v2_stream_t>();
    // if (s->flags & manapi::net::http::HTTP2_STREAM_PRIORITY_LOCKED)
    //     return false;
    if ((s->ctx->flags & manapi::net::http::HTTP2_CTX_FLAG_BLOCK_WRITE))
        return false;

    return true;
}

int default_wrk_http2_want_write (const manapi::net::worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto s = conn->as<manapi::net::http::http_v2_stream_t>();
    s->ctx->worker->event_toggle(s->ctx->conn, true, manapi::ev::WRITE);
    return 0;
}

manapi::net::worker::connection::ipdata_t * default_wrk_http2_ipdata (manapi::net::worker::connection *conn) MANAPIHTTP_NOEXCEPT {
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