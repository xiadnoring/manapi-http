#include "../include/http/ManapiNgHttp3Interface.hpp"
#include "ManapiUtils.hpp"

#ifdef MANAPIHTTP_NGHTTP3_DEPENDENCY && MANAPIHTTP_NGHTTP3_DEPENDENCY

#   include <cstring>

#   include "nghttp3/nghttp3.h"
#   include "nghttp3/version.h"

#   include "ManapiHttpResponse.hpp"
#   include "components/ManapiURLDecodeStream.hpp"
#   include "http/ManapiBaseHttp.hpp"
#   include "../include/ManapiSiteInternal.hpp"

static_assert(NGHTTP3_VERSION_NUM >= 0x010000, "libnghttp3 version must be greater than v1.0.0");

extern manapi::net::worker::http_v3_callbacks_t ng_wrk_http3_callbacks;

enum http3_stream_flags {
    HTTP3_STREAM_WANT_READ = manapi::ev::READ,
    HTTP3_STREAM_WANT_WRITE = manapi::ev::WRITE,
    HTTP3_STREAM_CLOSED = manapi::ev::DISCONNECT,
    HTTP3_STREAM_REMOVED = manapi::net::worker::base::CONN_REMOVED,
    HTTP3_STREAM_RECV_END = manapi::net::worker::base::CONN_RECV_END,
    HTTP3_STREAM_SEND_END  = manapi::net::worker::base::CONN_SEND_END,
    HTTP3_STREAM_IO_WAITING = manapi::net::worker::base::CONN_IO_WAITING,
    HTTP3_STREAM_TOP_READ = manapi::net::worker::base::CONN_TOP_READ,
    HTTP3_STREAM_IS_READING = manapi::net::worker::base::CONN_MAX_CODE << 1,
    HTTP3_STREAM_IS_DATA_STREAM = manapi::net::worker::base::CONN_MAX_CODE << 2,
    //HTTP3_STREAM_START_WORK_WAIT = 2048
};

struct manapi_nghttp3_conn_deleter {
    void operator () (nghttp3_conn *conn) const MANAPIHTTP_NOEXCEPT {
        nghttp3_conn_del(conn);
    }
};

struct manapi::net::worker::ng_wrk_http3_ctx_global_t {
    uint8_t flags;
    std::shared_ptr<net::worker::http_v3> http3;
    net::worker::base *worker;
    uint64_t max_concurrent_streams;
    uint64_t initial_max_stream_data_bidi_remote;
};

struct manapi::net::worker::ng_wrk_http3_ctx_t {
    shared_conn conn;
    nghttp3_settings h3_settings;
    std::unique_ptr<nghttp3_conn, manapi_nghttp3_conn_deleter> ctx;
    ng_wrk_http3_ctx_global_t *gctx;
    manapi::timer rtt_shutdown;
    uint32_t active_connections;
};

struct ng_wrk_http3_stream_t : manapi::net::worker::http_v3_stream_base_t {
    manapi::net::worker::ng_wrk_http3_ctx_t *ctx;
    manapi::net::worker::shared_conn s;
    std::unique_ptr<manapi::net::http::request_data_t> req;

    manapi::ev::buff_t *buffs;
    ssize_t size;
};

enum ng_wrk_http3_ctx_flags {
    WRKHTTP3_GCTX_FLAG_QLOG = 1
};

struct nghttp3_nv_deleter {
    void operator () (nghttp3_nv *src) const MANAPIHTTP_NOEXCEPT {
        delete[] src;
    }
};

#   define MANAPI_AS_STREAM(n__) (static_cast<ng_wrk_http3_stream_t *>(n__))
#   define MANAPI_AS_CONN(n__) (static_cast<manapi::net::worker::ng_wrk_http3_ctx_t *>(n__))

static int ng_wrk_http3_flush_write (manapi::net::worker::ng_wrk_http3_ctx_t *ctx) MANAPIHTTP_NOEXCEPT {
    int64_t v_stream_id;
    nghttp3_vec vec[4];

    while (ctx && ctx->conn) {
        int pfin = 0;

        auto vec_len = nghttp3_conn_writev_stream(ctx->ctx.get(), &v_stream_id, &pfin, vec, sizeof (vec) / sizeof (nghttp3_vec));

        if (vec_len < 0) {
            manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s: %s failed due to %s", "nghttp3",
                "nghttp3_conn_writev_stream", nghttp3_strerror(vec_len));
            break;
        }

        if (v_stream_id < 0)
            break;

        auto v_conn = ctx->gctx->worker->stream_id(ctx->conn, v_stream_id);
        if (!v_conn) {
            ctx->gctx->worker->close_connection(ctx->conn, manapi::net::worker::CLOSE_CONN_ERR);
            break;
        }

        assert (v_conn->wrk.data);

        auto v_s = MANAPI_AS_STREAM(v_conn->wrk.data);
        int interruped = 0;
        std::size_t res = 0;

        if (vec_len) {
            for (nghttp3_ssize i = 0; i < vec_len; i++) {
                auto &b = vec[i];
                auto rhs = ctx->gctx->worker->sync_write_ex (v_conn, b.base,
                    b.len, pfin && i + 1 == vec_len, 1e5);
                if (rhs !=b.len)
                    goto err;
                res += static_cast<std::size_t>(rhs);
                // if (rhs != b.len) {
                //     interruped = 1;
                //     break;
                // }
            }
        }
        else if (pfin) {
            if (ctx->gctx->worker->sync_write(v_conn, static_cast<char*>(nullptr), 0, true))
                goto err;
        }

        if (auto err = nghttp3_conn_add_write_offset(ctx->ctx.get(), v_stream_id, res)) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %s",
                "nghttp3", "nghttp3_conn_add_write_offset", nghttp3_strerror(err));
        }

        if (auto err = nghttp3_conn_add_ack_offset(ctx->ctx.get(), v_stream_id, res)) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %s",
                "nghttp3", "nghttp3_conn_add_write_offset", nghttp3_strerror(err));
        }

        if (ctx->gctx->flags & WRKHTTP3_GCTX_FLAG_QLOG) {
            manapi_log_debug("%s: send id=%zu fin=%d size=%zu", "nghttp3",
            v_stream_id,pfin, res);
        }

        if (interruped) {
            if (!(ctx->gctx->worker->event_flags(v_conn) & manapi::ev::WRITE))
                ctx->gctx->worker->event_toggle(v_conn, true, manapi::ev::WRITE);

            break;
        }

        continue;

        err: {
            ctx->gctx->http3->close_connection(v_conn, manapi::net::worker::CLOSE_CONN_ERR);
            break;
        }
    }

    return manapi::ERR_OK;
}

static ssize_t ng_wrk_http3_write (const manapi::net::worker::shared_conn &conn, manapi::ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT {
    auto const s = MANAPI_AS_STREAM (conn->wrk.data);
    auto const ctx = s->ctx->ctx.get();
    auto const stream_id = static_cast<int64_t>(s->ctx->gctx->worker->stream_id(conn));

    if (s->flags & (HTTP3_STREAM_CLOSED) || stream_id < 0)
        return -manapi::ERR_ABORTED;

    if (!s->ctx->gctx->worker->is_writable(conn))
        return 0;

    s->buffs = buff;
    s->size = static_cast<ssize_t>(nbuff);

    if (finish)
        s->flags |= HTTP3_STREAM_SEND_END;

    if (const auto err = nghttp3_conn_resume_stream(ctx, stream_id)) {
        if (err != NGHTTP3_ERR_INVALID_ARGUMENT) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed due to %s",
                "nghttp3", "nghttp3_conn_resume_stream", nghttp3_strerror(err));
            return -1;
        }
    }

    if (auto rhs = ng_wrk_http3_flush_write(s->ctx)) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %s failed",
            "nghttp3", "ng_wrk_http3_flush_write");
        goto err;
    }

    if (s->buffs) {
        /* it means that send chunks cb was not executed */
        if (finish && s->flags & HTTP3_STREAM_SEND_END)
            s->flags ^= HTTP3_STREAM_SEND_END;

        s->buffs = nullptr;
        return 0;
    }

    return s->size;

    err: {
        s->buffs = nullptr;

        if (s->flags & HTTP3_STREAM_IS_DATA_STREAM) {
            s->ctx->gctx->http3->close_connection(s->s, manapi::net::worker::CLOSE_CONN_ERR);
        }
        else {
            if (s->s)
                s->ctx->gctx->worker->close_connection(s->s, manapi::net::worker::CLOSE_CONN_ERR);
        }

        return -manapi::ERR_INTERNAL;
    }
}

static int ng_wrk_http3_global_cleanup (manapi::net::worker::wrk_interface_global_t *data, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto ctx = static_cast<manapi::net::worker::ng_wrk_http3_ctx_global_t *> (data->data);
    delete ctx;
    data->data = nullptr;
    return manapi::ERR_OK;
}

static int ng_wrk_http3_cleanup (manapi::net::worker::connection *conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    if (conn->wrk.flags & manapi::net::worker::WRK_INTERFACE_IS_STREAM) {
        auto wrk_data = static_cast<ng_wrk_http3_stream_t *>(conn->wrk.data);
        delete wrk_data;

        conn->wrk.flags = 0;
        conn->wrk.data = nullptr;
    }
    else {
        auto wrk_data = static_cast<manapi::net::worker::ng_wrk_http3_ctx_t *>(conn->wrk.data);
        delete wrk_data;
        conn->wrk.flags = 0;
        conn->wrk.data = nullptr;
    }
    return manapi::ERR_OK;
}

static int ng_wrk_http3(const manapi::net::worker::shared_conn &stream, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto s = MANAPI_AS_STREAM (stream->wrk.data);

    assert (s);

    if (!(flags & manapi::ev::DISCONNECT)) {


        assert(s->s);

        int64_t stream_id = s->ctx->gctx->worker->stream_id(s->s);

        if (stream_id < 0)
            return manapi::ERR_INTERNAL;

        if (s->ctx->gctx->flags & WRKHTTP3_GCTX_FLAG_QLOG) {
            manapi_log_debug("%s: recv data flags=%d id=%zu size=%zu",
                "nghttp3", flags, stream_id, nsize);
        }

        if (flags & manapi::ev::WRITE) {
            //assert(s->top->send.deque);
            if (s->flags & manapi::ev::WRITE) {
                s->ctx->gctx->http3->feed_event(stream, manapi::ev::WRITE, nullptr, 0, nullptr);

                if (!s->top->send.deque) {
                    s->ctx->gctx->worker->event_toggle(stream, false, manapi::ev::WRITE);
                }
            }

            ng_wrk_http3_flush_write (s->ctx);
        }

        if (flags & manapi::net::worker::base::CONN_READ) {
            auto rhs = nghttp3_conn_read_stream (s->ctx->ctx.get(), stream_id, reinterpret_cast<const uint8_t *>(buffer),
                nsize, flags & manapi::net::worker::base::CONN_RECV_END);

            if (rhs < 0) {
                manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s: %s failed due to %s",
                    "nghttp3", "nghttp3_conn_read_stream", nghttp3_strerror(rhs));
                goto err;
            }

            ng_wrk_http3_flush_write (s->ctx);
        }
        else if (flags & (manapi::net::worker::base::CONN_RECV_END)) {
            if (!(s->flags & manapi::net::worker::base::CONN_RECV_END)) {
                auto rhs = nghttp3_conn_read_stream (s->ctx->ctx.get(), stream_id, nullptr,
                    0, true);

                if (rhs < 0 && (rhs != NGHTTP3_ERR_H3_FRAME_UNEXPECTED && rhs != NGHTTP3_ERR_MALFORMED_HTTP_MESSAGING)) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_LOW,
                        "%s: %s failed due to %s", "nghttp3", "nghttp3_conn_shutdown_stream_read", nghttp3_strerror(rhs));
                }

                s->ctx->gctx->worker->event_toggle(s->s, false, manapi::ev::READ);
            }
        }

        return manapi::ERR_OK;
    }

    err: {
        auto s = MANAPI_AS_STREAM (stream->wrk.data);

        if (s) {
            if((s->flags & HTTP3_STREAM_IS_DATA_STREAM))
                s->ctx->gctx->http3->close_connection(stream, manapi::net::worker::CLOSE_CONN_ERR);
            else {
                if (s->s)
                    s->ctx->gctx->worker->close_connection(s->s, manapi::net::worker::CLOSE_CONN_ERR);
            }
        }

        return manapi::ERR_INTERNAL;
    }
}

static int ng_wrk_http3_stream_init (const manapi::net::worker::shared_conn &conn, const manapi::net::worker::shared_conn &stream, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    assert (conn->wrk.data);
    assert (!stream->wrk.data);
    assert (!(conn->wrk.flags & manapi::net::worker::WRK_INTERFACE_IS_STREAM));
    assert (stream->wrk.flags & manapi::net::worker::WRK_INTERFACE_IS_STREAM);

    auto conn_data = static_cast<manapi::net::worker::ng_wrk_http3_ctx_t *> (conn->wrk.data);

    if (!conn_data || !conn_data->conn || !conn_data->ctx)
        return manapi::ERR_ABORTED;

    std::unique_ptr<ng_wrk_http3_stream_t> tp (new (std::nothrow) ng_wrk_http3_stream_t{});

    if (!tp)
        return manapi::ERR_RESOURCE_EXHAUSTED;

    tp->ctx = conn_data;
    tp->top.reset(new (std::nothrow) manapi::net::worker::connection_io{});
    tp->s = stream;

    if (!tp->top)
        return manapi::ERR_RESOURCE_EXHAUSTED;

    auto const stream_id = conn_data->gctx->worker->stream_id(stream);

    if (stream_id < 0)
        return manapi::ERR_NOT_FOUND;

    stream->wrk.data = tp.release();

    try {
        w->event_on(stream,
            [w, global]
            (const manapi::net::worker::shared_conn & conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p)
            -> void {
            ng_wrk_http3(conn, flags, buffer, nsize, p, global, w);
        });

        w->event_flags(stream, manapi::ev::READ);
    }
    catch (std::bad_alloc const &) {
        return manapi::ERR_RESOURCE_EXHAUSTED;
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return manapi::ERR_INTERNAL;
    }

    if (conn_data->gctx->flags & WRKHTTP3_GCTX_FLAG_QLOG) {
        manapi_log_debug("%s: stream id=%zu was initialized", "nghttp3",
            stream_id);
    }

    return manapi::ERR_OK;
}

static int ng_wrk_http3_acked_stream_data (nghttp3_conn *conn, int64_t stream_id, uint64_t datalen, void *conn_user_data, void *stream_user_data)  MANAPIHTTP_NOEXCEPT {
    auto s = MANAPI_AS_STREAM(stream_user_data);
    if (s) {
        if (s->ctx->gctx->flags & WRKHTTP3_GCTX_FLAG_QLOG) {
            manapi_log_debug("%s: stream id=%zu ack=%zu conn=%p", "nghttp3", stream_id, datalen, conn);
        }
    }
    return 0;
}

static int ng_wrk_http3_begin_headers (nghttp3_conn *conn, int64_t stream_id, void *conn_user_data, void *stream_user_data)  MANAPIHTTP_NOEXCEPT {
    auto conn_data = MANAPI_AS_CONN(conn_user_data);
    assert(conn_data && conn_data->conn);
    auto stream = conn_data->gctx->worker->stream_id(conn_data->conn, stream_id);
    if (!stream)
        return NGHTTP3_ERR_STREAM_NOT_FOUND;

    auto const s = MANAPI_AS_STREAM(stream->wrk.data);

    if (!s)
        return NGHTTP3_ERR_FATAL;

    conn_data->gctx->worker->waiting(stream, true);

    s->flags |= HTTP3_STREAM_IS_DATA_STREAM;

    if (auto rhs = nghttp3_conn_set_stream_user_data (conn, stream_id, s)) {
        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s: %s failed due to %s", "nghttp3", "nghttp3_conn_set_stream_user_data",
            nghttp3_strerror(rhs));
        return manapi::ERR_INTERNAL;
    }

    if (!s->req)
        s->req.reset(new (std::nothrow) manapi::net::http::request_data_t{});


    if (!s->req)
        return NGHTTP3_ERR_NOMEM;

    s->req->http = manapi::net::http::versions::HTTP_v3;

    return 0;
}

static int ng_wrk_http3_begin_trailers (nghttp3_conn *conn, int64_t stream_id, void *conn_user_data, void *stream_user_data) MANAPIHTTP_NOEXCEPT {
    return 0;
}

static int ng_wrk_http3_deferred_consume (nghttp3_conn *conn, int64_t stream_id, size_t consumed, void *conn_user_data, void *stream_user_data) MANAPIHTTP_NOEXCEPT {
    auto s = MANAPI_AS_STREAM(stream_user_data);
    if (s) {
        if (s->ctx->gctx->flags & WRKHTTP3_GCTX_FLAG_QLOG) {
            manapi_log_debug("%s: stream id=%zu consumed=%zu conn=%p", "nghttp3", stream_id, consumed, conn);
        }
    }
    return 0;
}


static void ng_wrk_http3_flush_close (manapi::net::worker::ng_wrk_http3_ctx_t *ctx) MANAPIHTTP_NOEXCEPT {
    if (ctx->conn && (ctx->gctx->worker->event_flags(ctx->conn) & manapi::net::worker::base::CONN_WANT_CLOSE)
        && !ctx->gctx->worker->streams_size(ctx->conn)) {

        if (nghttp3_conn_is_drained(ctx->ctx.get())) {
            auto conn = ctx->conn;
            if (ctx->rtt_shutdown) {
                ctx->rtt_shutdown.stop();
                ctx->rtt_shutdown = nullptr;
            }
            ctx->gctx->worker->close_connection(conn, manapi::net::worker::CLOSE_CONN_SHUTDOWN);
        }
    }
}

static int ng_wrk_http3_end_headers (nghttp3_conn *conn, int64_t stream_id, int fin, void *conn_user_data, void *stream_user_data) MANAPIHTTP_NOEXCEPT {
    try {
        auto s = MANAPI_AS_STREAM(stream_user_data);

        if (!s || !s->s)
            return 0;

        if (fin)
            s->flags |= HTTP3_STREAM_RECV_END;

        s->ctx->gctx->worker->waiting(s->s, false);

        int status = manapi::net::http::OK_200;

        auto heit = s->req->headers.extract(":path");
        if (heit.empty()) {
            status = manapi::net::http::BAD_REQUEST_400;
            s->req->uri = "/";
            s->req->divided = -1;
        }
        else {
            s->req->uri = std::move(heit.mapped());
            manapi::net::http::url_decode_stream url_decoder;
            if (auto rhs = url_decoder << s->req->uri) {
                s->req->divided = -1;
                s->req->uri = "/";

                status = manapi::net::http::BAD_REQUEST_400;
            }
            else {
                s->req->path = url_decoder.result();
                s->req->divided = url_decoder.divided();
            }
        }

        heit = s->req->headers.extract(":method");
        if (heit.empty()) {
            status = manapi::net::http::BAD_REQUEST_400;
            s->req->method = "GET";
        }
        else {
            s->req->method = std::move(heit.mapped());
        }

        auto hit = s->req->headers.find(manapi::net::http::HEADER.CONTENT_LENGTH);
        if (hit == s->req->headers.end()) {
            s->req->body_size = -1;
        }
        else {
            try {
                s->req->body_size = std::stoll(hit->second);
            }
            catch (std::exception const &e) {
                manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s: %s",
                    "nghttp3", e.what());
                s->req->body_size = -1;
            }

            if (s->req->body_size < 0) {
                status = manapi::net::http::BAD_REQUEST_400;
                s->req->body_size = 0;
            }
        }

        manapi::async::current()->etaskpool()->append_task(
            [status, conn = s->s] () -> void {
                auto s = MANAPI_AS_STREAM (conn->wrk.data);
                if (!s)
                    return;

                auto &ctx = s->ctx;
                auto const stream_id = ctx->gctx->worker->stream_id(conn);

                if (stream_id < 0) {
                    return;
                }

                auto const req_ptr = s->req.get();
                manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM,
                    "http3: %zu stream on %.*s", stream_id, req_ptr->uri.size(), req_ptr->uri.data());
                auto cdata = std::make_unique<manapi::net::http::internal::handle_data_t>(conn, ctx->gctx->http3,
                    req_ptr, std::make_unique<manapi::net::http::internal::cont_callback_cb_t>(
                    [conn, req = std::move(s->req)] (bool ok) mutable
                    -> void {
                        manapi::async::current()->etaskpool()->append_task(
                            [ok, conn = std::move(conn)] () mutable  -> void {
                                auto s = MANAPI_AS_STREAM(conn->wrk.data);

                                if (!s) {
                                    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s: %s failed due to %s",
                                        "nghttp3", "stream finish", "doesn't exists");
                                    return;
                                }

                                auto &ctx = s->ctx;

                                auto flags = ok ? manapi::net::worker::CLOSE_CONN_SHUTDOWN : manapi::net::worker::CLOSE_CONN_ERR;

                                ctx->active_connections--;
                                ctx->gctx->http3->close_connection(conn, flags);

                                ng_wrk_http3_flush_close (s->ctx);
                        });
                }));

                ctx->active_connections++;

                // this->event_on(conn, std::unique_ptr<worker_watcher_cb>(nullptr));
                // this->event_flags(conn, 0);

                cdata->router = ctx->gctx->worker->site().handler(req_ptr);
                manapi::net::http::internal::handle_income_request(std::move(cdata), status);
        });
    }
    catch (std::bad_alloc const &) {
        return NGHTTP3_ERR_NOMEM;
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return NGHTTP3_ERR_FATAL;
    }

    return 0;
}

static int ng_wrk_http3_end_stream (nghttp3_conn *conn, int64_t stream_id, void *conn_user_data, void *stream_user_data) MANAPIHTTP_NOEXCEPT {
    auto s = MANAPI_AS_STREAM(stream_user_data);
    if (!s)
        return 0;

    auto const config = s->ctx->gctx->worker->config();

    s->flags |= manapi::net::worker::base::CONN_RECV_END;

    if (s->top->recv_size) {
        if (auto err = manapi::net::worker::http_v3_flush_recv(config, s->s, s)) {
            switch (err) {
                case manapi::ERR_RESOURCE_EXHAUSTED:
                    return NGHTTP3_ERR_NOMEM;
                default:
                    return NGHTTP3_ERR_FATAL;
            }
        }
    }
    else {
        s->ctx->gctx->http3->feed_event(s->s, manapi::net::worker::base::CONN_RECV_END,
            nullptr, 0, nullptr);
    }

    return 0;
}

static int ng_wrk_http3_end_trailers (nghttp3_conn *conn, int64_t stream_id, int fin, void *conn_user_data, void *stream_user_data) MANAPIHTTP_NOEXCEPT {
    return 0;
}

static int ng_wrk_http3_recv_data (nghttp3_conn *conn, int64_t stream_id, const uint8_t *data, size_t datalen, void *conn_user_data, void *stream_user_data) MANAPIHTTP_NOEXCEPT {
    auto s = MANAPI_AS_STREAM(stream_user_data);
    if (!s)
        return 0;

    auto const config = s->ctx->gctx->worker->config();

    auto rhs = manapi::net::worker::base::connection_io_send(&s->top->recv, reinterpret_cast<const char*>(data), datalen, &s->ctx->gctx->worker->bufferpool(),
        config->buffer_size, &s->top->recv_size, 1e5);

    if (rhs != datalen)
        return NGHTTP3_ERR_NOMEM;

    if (auto err = manapi::net::worker::http_v3_flush_recv(config, s->s, s)) {
        switch (err) {
            case manapi::ERR_RESOURCE_EXHAUSTED:
                return NGHTTP3_ERR_NOMEM;
            default:
                return NGHTTP3_ERR_FATAL;
        }
    }

    if (manapi::net::worker::prepared::read_buffs_is_full(s->top.get(), config)) {
        s->ctx->gctx->worker->event_toggle(s->s, false, manapi::ev::READ);
    }

    return 0;
}

static int ng_wrk_http3_recv_header (nghttp3_conn *conn, int64_t stream_id, int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags, void *conn_user_data, void *stream_user_data) MANAPIHTTP_NOEXCEPT {
    auto s = MANAPI_AS_STREAM(stream_user_data);
    int ret = 0;

    try {
        auto name_vec = nghttp3_rcbuf_get_buf(name);
        auto value_vec = nghttp3_rcbuf_get_buf(value);

        std::string_view name_str (reinterpret_cast<const char*>(name_vec.base), name_vec.len);
        auto it = s->req->headers.find(name_str);
        if (it == s->req->headers.end()) {
            s->req->headers.insert({std::string{name_str},
                std::string(reinterpret_cast<const char *>(value_vec.base), value_vec.len)});
        }
        else {
            it->second.append(", ");
            it->second.append(std::string_view(reinterpret_cast<const char *> (value_vec.base),
                value_vec.len));
        }
    }
    catch (std::exception const &e) {
        ret = NGHTTP3_ERR_NOMEM;
    }

    return ret;
}

static int ng_wrk_http3_recv_settings (nghttp3_conn *conn, const nghttp3_settings *settings, void *conn_user_data) {
    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "nghttp3: %s settings on %p: "
        "enable_connect_procotol=%d, h3_datagram=%d max_field_section_size=%zu qpack_blocked_streams=%zu "
        "qpack_encoder_max_dtable_capacity=%zu qpack_max_dtable_capacity=%zu", "recv", conn, settings->enable_connect_protocol, settings->h3_datagram,
        settings->max_field_section_size, settings->qpack_blocked_streams, settings->qpack_encoder_max_dtable_capacity,
        settings->qpack_max_dtable_capacity);

    return 0;
}

static int ng_wrk_http3_recv_trailer (nghttp3_conn *conn, int64_t stream_id, int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags, void *conn_user_data, void *stream_user_data)  MANAPIHTTP_NOEXCEPT {
    return 0;
}

static int ng_wrk_http3_reset_stream (nghttp3_conn *conn, int64_t stream_id, uint64_t app_error_code, void *conn_user_data, void *stream_user_data)  MANAPIHTTP_NOEXCEPT {
    auto s = MANAPI_AS_STREAM(stream_user_data);
    if (!s || !s->s)
        return 0;

    s->ctx->gctx->http3->close_connection(s->s, manapi::net::worker::CLOSE_CONN_EOF);

    return 0;
}

static void ng_wrk_http3_shutdown_conn_next (manapi::net::worker::shared_conn conn) MANAPIHTTP_NOEXCEPT {
    auto data = MANAPI_AS_CONN(conn->wrk.data);

    if (data && data->conn) {
        if (auto rhs = nghttp3_conn_shutdown(data->ctx.get())) {
            manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s: %s failed due to %s",
                "nghttp3", "nghttp3_conn_shutdown", nghttp3_strerror(rhs));
            goto err;
        }

        if (ng_wrk_http3_flush_write (data)) {
            /* wow */
        }

        ng_wrk_http3_flush_close (data);
    }

    return;

    err: {
        auto streams = data->gctx->worker->streams_size(data->conn);
        if (data->rtt_shutdown) {
            data->rtt_shutdown.stop();
            data->rtt_shutdown = nullptr;
        }

        if (!streams && !data->active_connections) {
            data->conn = nullptr;
            data->gctx->worker->close_connection(conn, manapi::net::worker::CLOSE_CONN_ERR);
        }
    }
}

static int ng_wrk_http3_shutdown_conn (const manapi::net::worker::shared_conn & conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, bool force) MANAPIHTTP_NOEXCEPT {
    if (conn->wrk.flags & manapi::net::worker::WRK_INTERFACE_IS_STREAM) {
        auto data = MANAPI_AS_STREAM(conn->wrk.data);

        if (!data || !data->s)
            return 1;

        data->s = nullptr;

        return 1;
    }
    else {
        auto data = MANAPI_AS_CONN(conn->wrk.data);

        if (!data || !data->conn)
            return 1;

        auto const streams_size = data->gctx->worker->streams_size(data->conn);

        if (!data->ctx)
            goto err;

        if ((!streams_size && !data->active_connections)) {
            if (data->rtt_shutdown) {
                data->rtt_shutdown.stop();
                data->rtt_shutdown = nullptr;
            }

            data->conn = nullptr;

            return 1;
        }

        if (!data->rtt_shutdown) {
            if (auto rhs = nghttp3_conn_submit_shutdown_notice(data->ctx.get())) {
                manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s: %s failed due to %s",
                    "nghttp3", "nghttp3_conn_submit_shutdown_notice", nghttp3_strerror(rhs));
                goto err;
            }

            if (ng_wrk_http3_flush_write (data)) {
                /* wow */
            }

            try {
                auto rhs = manapi::async::current()->timerpool()->append_timer_sync(1000,
                    [conn] (const manapi::timer &t) mutable -> void {
                    ng_wrk_http3_shutdown_conn_next(std::move(conn));
                });

                data->rtt_shutdown = rhs.unwrap();
            }
            catch (std::bad_alloc const &) {
                goto err;
            }
            catch (std::exception const &e) {
                manapi_log_error(e.what());
                goto err;
            }
        }

        return 0;

        err: {
            if (!streams_size && !data->active_connections) {
                if (data->rtt_shutdown) {
                    data->rtt_shutdown.stop();
                    data->rtt_shutdown = nullptr;
                }

                data->conn = nullptr;
                return 1;
            }

            return 0;
        }
    }
}

static int ng_wrk_http3_shutdown (nghttp3_conn *conn, int64_t id, void *conn_user_data)  MANAPIHTTP_NOEXCEPT {
    auto ctx = MANAPI_AS_CONN(conn_user_data);

    if (!ctx || !ctx->conn)
        return 0;

    ctx->gctx->worker->close_connection(std::move(ctx->conn), manapi::net::worker::CLOSE_CONN_SHUTDOWN);

    return 0;
}

static int ng_wrk_http3_stop_sending (nghttp3_conn *conn, int64_t stream_id, uint64_t app_error_code, void *conn_user_data, void *stream_user_data)  MANAPIHTTP_NOEXCEPT {
    return 0;
}

static int ng_wrk_http3_stream_close (nghttp3_conn *conn, int64_t stream_id, uint64_t app_error_code, void *conn_user_data, void *stream_user_data)  MANAPIHTTP_NOEXCEPT {
    auto s = MANAPI_AS_STREAM(stream_user_data);


    if (s) {
        if (s->s)
            s->ctx->gctx->worker->close_connection(s->s, manapi::net::worker::CLOSE_CONN_SHUTDOWN);

        ng_wrk_http3_flush_close(s->ctx);
    }

    return 0;
}

static int ng_wrk_http3_init (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    try {
        assert(!conn->wrk.data);

        assert (!(conn->wrk.flags & manapi::net::worker::WRK_INTERFACE_IS_STREAM));

        auto tp = std::make_unique<manapi::net::worker::ng_wrk_http3_ctx_t>();

        /* nghttp2 init */
        nghttp3_callbacks callbacks;

        callbacks.acked_stream_data = ng_wrk_http3_acked_stream_data;

        callbacks.begin_headers = ng_wrk_http3_begin_headers;

        callbacks.begin_trailers = ng_wrk_http3_begin_trailers;

        callbacks.deferred_consume = ng_wrk_http3_deferred_consume;

        callbacks.end_headers = ng_wrk_http3_end_headers;

        callbacks.end_stream = ng_wrk_http3_end_stream;

        callbacks.end_trailers = ng_wrk_http3_end_trailers;

        callbacks.recv_data = ng_wrk_http3_recv_data;

        callbacks.recv_header = ng_wrk_http3_recv_header;

        callbacks.recv_settings = ng_wrk_http3_recv_settings;

        callbacks.recv_trailer = ng_wrk_http3_recv_trailer;

        callbacks.reset_stream = ng_wrk_http3_reset_stream;

        callbacks.shutdown = ng_wrk_http3_shutdown;

        callbacks.stop_sending = ng_wrk_http3_stop_sending;

        callbacks.stream_close = ng_wrk_http3_stream_close;

        auto global_ctx = static_cast<manapi::net::worker::ng_wrk_http3_ctx_global_t *> (global->data);
        tp->gctx = global_ctx;

        auto const config = tp->gctx->worker->config();

        nghttp3_settings_default(&tp->h3_settings);

        if (config->max_headers_size)
            tp->h3_settings.max_field_section_size = config->max_headers_size;
        if (config->max_hpack_table_size >= 0)
            tp->h3_settings.qpack_max_dtable_capacity = config->max_hpack_table_size;
        if (config->max_hpack_table_size >= 0)
            tp->h3_settings.qpack_encoder_max_dtable_capacity = config->max_hpack_table_size;

        nghttp3_conn *p;
        auto rhs = nghttp3_conn_server_new(&p, &callbacks, &tp->h3_settings, nghttp3_mem_default(), tp.get());
        if (rhs) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH,
                "nghttp3_conn_server_new failed %s", nghttp3_strerror(rhs));
            return manapi::ERR_INTERNAL;
        }

        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "nghttp3: %s settings on %p: "
            "enable_connect_procotol=%d, h3_datagram=%d max_field_section_size=%zu qpack_blocked_streams=%zu "
            "qpack_encoder_max_dtable_capacity=%zu qpack_max_dtable_capacity=%zu", "send", p, tp->h3_settings.enable_connect_protocol, tp->h3_settings.h3_datagram,
            tp->h3_settings.max_field_section_size, tp->h3_settings.qpack_blocked_streams, tp->h3_settings.qpack_encoder_max_dtable_capacity,
            tp->h3_settings.qpack_max_dtable_capacity);

        tp->ctx.reset(p);

        tp->conn = conn;

        nghttp3_conn_set_max_concurrent_streams(p, tp->gctx->max_concurrent_streams);
        nghttp3_conn_set_max_client_streams_bidi (p, tp->gctx->initial_max_stream_data_bidi_remote);

        conn->wrk.data = tp.release();

        auto res = w->new_stream(conn, manapi::net::worker::base::CONN_STREAM_FLAG_UNI);
        if (!res.ok())
            return manapi::ERR_ABORTED;

        auto ctrl_stream = res.unwrap();

        res = w->new_stream(conn, manapi::net::worker::base::CONN_STREAM_FLAG_UNI);
        if (!res.ok())
            return manapi::ERR_ABORTED;

        auto encode_stream = res.unwrap();

        res = w->new_stream(conn, manapi::net::worker::base::CONN_STREAM_FLAG_UNI);
        if (!res.ok())
            return manapi::ERR_ABORTED;

        auto decode_stream = res.unwrap();

        auto ctrl_stream_id = w->stream_id(ctrl_stream);
        auto encode_stream_id = w->stream_id(encode_stream);
        auto decode_stream_id = w->stream_id(decode_stream);

        if (ctrl_stream_id < 0 || encode_stream_id < 0 || decode_stream_id < 0)
            return manapi::ERR_INTERNAL;

        if (auto err = nghttp3_conn_bind_qpack_streams(p, encode_stream_id, decode_stream_id)) {
            manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s: %s failed due to %s",
                "nghttp3", "nghttp3_conn_bind_qpack_streams", nghttp3_strerror(err));
            return manapi::ERR_INTERNAL;
        }
        if (auto err = nghttp3_conn_bind_control_stream(p, ctrl_stream_id)) {
            manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s: %s failed due to %s",
                "nghttp3", "nghttp3_conn_bind_control_stream", nghttp3_strerror(err));
            return manapi::ERR_INTERNAL;
        }

        return manapi::ERR_OK;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "ng_wrk_http3_init", e.what());
        return manapi::ERR_UNKNOWN;
    }
}

static nghttp3_ssize ng_wrk_http3_read_data (nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec, size_t veccnt, uint32_t *pflags, void *conn_user_data, void *stream_user_data) {
    auto sess = MANAPI_AS_CONN(conn_user_data);
    auto s = MANAPI_AS_STREAM(stream_user_data);

    assert(sess && s);

    *pflags = 0;

    /* send data */
    ssize_t cnt = 0;

    auto buffs = s->buffs;
    s->buffs = nullptr;

    if (!s->size || !buffs) {

        if (s->flags & HTTP3_STREAM_SEND_END) {
            (*pflags) |= NGHTTP3_DATA_FLAG_EOF;
            return 0;
        }

        return NGHTTP3_ERR_WOULDBLOCK;
    }

    std::size_t res = 0;

    if (buffs) {
        while (veccnt && s->size) {
            vec->base = reinterpret_cast<uint8_t *>(buffs->base);
            vec->len = buffs->len;

            res += buffs->len;

            vec++;
            veccnt--;

            buffs++;
            s->size--;

            cnt++;

            break;
        }

        if (s->flags & HTTP3_STREAM_SEND_END) {
            if (s->size)
                s->flags ^= HTTP3_STREAM_SEND_END;
            else
                (*pflags) |= NGHTTP3_DATA_FLAG_EOF;
        }
    }

    s->size = res;

    return cnt;
}

static int ng_wrk_http3_send_response_sync (const manapi::net::worker::shared_conn &stream, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) {
    auto const s = MANAPI_AS_STREAM(stream->wrk.data);
    auto &ctx = s->ctx;

    auto &headers = res->headers();
    auto const hs = headers.size() + 1;
    nghttp3_nv p[hs];
    std::size_t i = 0;
    /* 0 is reserved for :status */
    i = 1;

    for (auto &[h, v] : headers) {
        auto &nv = p[i++];
        nv.name = (uint8_t*)h.data();
        nv.namelen = h.size();
        nv.value = reinterpret_cast<uint8_t*>(v.data());
        nv.valuelen = v.size();
        nv.flags = NGHTTP3_NV_FLAG_NO_COPY_NAME|NGHTTP3_NV_FLAG_NO_COPY_VALUE;
    }

    i = 0;
    auto rit = headers.insert({":status", std::to_string(res->status_code())});
    auto it = rit.first;
    auto &nv = p[i++];
    nv.name = (uint8_t*)it->first.data();
    nv.namelen = it->first.size();
    nv.value = reinterpret_cast<uint8_t*>(it->second.data());
    nv.valuelen = it->second.size();
    nv.flags = NGHTTP3_NV_FLAG_NO_COPY_NAME|NGHTTP3_NV_FLAG_NO_COPY_VALUE;

    nghttp3_data_reader dr;
    dr.read_data = ng_wrk_http3_read_data;
    auto stream_id = ctx->gctx->worker->stream_id(stream);
    if (stream_id < 0)
        return manapi::ERR_INTERNAL;

    if (finish)
        s->flags |= HTTP3_STREAM_SEND_END;

    auto const rhs = nghttp3_conn_submit_response(ctx->ctx.get(), stream_id,
        p, hs, &dr);

    if (rhs) {
        manapi_log_trace("%s: %s failed due to %s", "nghttp3", "nghttp3_conn_submit_response",
            nghttp3_strerror(rhs));
        return manapi::ERR_INTERNAL;
    }

    ng_wrk_http3_flush_write(ctx);

    return manapi::ERR_OK;
}

static manapi::future<int> ng_wrk_http3_send_response (const manapi::net::worker::shared_conn &stream, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) {
    co_return ng_wrk_http3_send_response_sync(stream, global, w, res, finish);
}

manapi::error::status manapi::net::worker::ng_wrk_http3_global_init(manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::interface_worker *w) MANAPIHTTP_NOEXCEPT {
    try {
        assert(!global->data);

        if (!(w->worker_flags() & WORKER_BASE_FLAG_MULTISTREAM))
            return error::status_invalid_argument("worker doesn't support multistream");


        auto tp = std::make_unique<ng_wrk_http3_ctx_global_t>();

        tp->http3 = std::make_shared<worker::http_v3>(w, &ng_wrk_http3_callbacks);
        // tp->worker->init(0);
        tp->worker = w;

        auto config = w->config();

        typedef manapi::internal::config_interface cv;

        auto const quic_debug = cv::get_config_param<bool>(config->quic, "debug", false);

        tp->initial_max_stream_data_bidi_remote =  cv::get_config_param<uint64_t>(config->quic, "initial_max_stream_data_bidi_remote", 1000000);
        tp->max_concurrent_streams = config->max_concurrent_streams > 0 ? config->max_concurrent_streams : 6;

        if (quic_debug)
            tp->flags |= WRKHTTP3_GCTX_FLAG_QLOG;

        global->flags |= WRK_GLOBAL_FLAG_MULTISTREAM|WRK_GLOBAL_FLAG_SHUTDOWN_SUPPORTED;
        global->data = tp.release();

        global->accept_cb = ng_wrk_http3;
        global->init_cb = ng_wrk_http3_init;
        global->init_stream_cb = ng_wrk_http3_stream_init;
        global->cleanup_cb = ng_wrk_http3_cleanup;
        global->cleanup_global_cb = ng_wrk_http3_global_cleanup;
        global->flush_custom_read_cb = nullptr;
        global->update_limit_rate = nullptr;
        global->custom_read_cb = nullptr;
        global->send_response = ng_wrk_http3_send_response;
        global->shutdown_cb = ng_wrk_http3_shutdown_conn;


        return error::status_ok();
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "nghttp3: init", e.what());
        return error::status_internal("nghttp3: init");
    }
}

static int ng_wrk_http3_on_read_stream (const manapi::net::worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const s = MANAPI_AS_STREAM (conn->wrk.data);

    if (!s || !s->s)
        return manapi::ERR_OK;

    auto const config = s->ctx->gctx->worker->config();

    if (auto const rhs = manapi::net::worker::http_v3_flush_recv (config, conn, s))
        return rhs;

    if (!manapi::net::worker::prepared::read_buffs_is_full(s->top.get(), config)) {
        auto const stream_id = s->ctx->gctx->worker->stream_id(s->s);
        if (stream_id < 0)
            return manapi::ERR_INTERNAL;
        if (s->ctx->gctx->flags & WRKHTTP3_GCTX_FLAG_QLOG)
            manapi_log_debug("%s: %zu read start in %p", "nghttp3", stream_id, s->ctx->conn.get());

        s->ctx->gctx->worker->event_toggle(s->s, true, manapi::ev::READ);
        if ((s->flags & manapi::ev::READ)) {

        }
    }

    return manapi::ERR_OK;
}

static int ng_wrk_http3_want_write (const manapi::net::worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const s = MANAPI_AS_STREAM(conn->wrk.data);

    if (s)
        s->ctx->gctx->worker->event_toggle(conn, true, manapi::ev::WRITE);

    return 0;
}

static int ng_wrk_http3_rst (const manapi::net::worker::shared_conn &conn, int code) MANAPIHTTP_NOEXCEPT {
    auto const s = MANAPI_AS_STREAM(conn->wrk.data);

    //if ((s->flags & manapi::ev::DISCONNECT)) {
    auto const sid = static_cast<int64_t>(s->ctx->gctx->worker->stream_id(conn));
    if (sid < 0)
        return manapi::ERR_NOT_FOUND;

    if (s->flags & HTTP3_STREAM_IS_DATA_STREAM) {
        if (auto rhs = nghttp3_conn_close_stream(s->ctx->ctx.get(), sid, code)) {
            manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s: %s failed due to %s",
                "nghttp3", "nghttp3_conn_close_stream", nghttp3_strerror(rhs));

            return manapi::ERR_INTERNAL;
        }
    }
    else {
        if (s->s)
            s->ctx->gctx->worker->close_connection(s->s, manapi::net::worker::CLOSE_CONN_ERR);

        if (!s->ctx->active_connections)
            s->ctx->gctx->worker->close_connection(s->ctx->conn, manapi::net::worker::CLOSE_CONN_ERR);
    }

    return manapi::ERR_OK;
}

static bool ng_wrk_http3_is_writable (const manapi::net::worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const s = MANAPI_AS_STREAM (conn->wrk.data);

    //auto const s = conn->as<http_v2_stream_t>();
    //auto const config = http_v2_ctx->gctx->base_worker->config();

    //return nghttp2_session_want_write(http_v2_ctx->ctx.get());
    return s->ctx->gctx->worker->is_writable(conn);
}

static manapi::net::worker::connection::ipdata_t * ng_wrk_http3_ipdata (manapi::net::worker::connection *conn) MANAPIHTTP_NOEXCEPT {
    auto const s = MANAPI_AS_STREAM (conn->wrk.data);
    return s->ctx->gctx->http3->ipdata(conn);
}

manapi::net::worker::http_v3_callbacks_t ng_wrk_http3_callbacks {
    .http_v3_write = ng_wrk_http3_write,
    .http_v3_on_read_stream = ng_wrk_http3_on_read_stream,
    .http_v3_want_write = ng_wrk_http3_want_write,
    .http_v3_rst_stream = ng_wrk_http3_rst,
    .http_v3_is_writable = ng_wrk_http3_is_writable,
    .http_v3_ip_data = ng_wrk_http3_ipdata
};

#endif