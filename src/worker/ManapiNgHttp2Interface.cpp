#include "../include/worker/ManapiNgHttp2Interface.hpp"

#include <cstring>

#include "ManapiHttpResponse.hpp"
#include "nghttp2/nghttp2.h"
#include "nghttp2/nghttp2ver.h"

extern manapi::net::worker::http_v2_callbacks_t ng_wrk_http2_callbacks;

enum http2_stream_flags {
    HTTP2_STREAM_WANT_READ = manapi::ev::READ,
    HTTP2_STREAM_WANT_WRITE = manapi::ev::WRITE,
    HTTP2_STREAM_CLOSED = manapi::ev::DISCONNECT,
    HTTP2_STREAM_REMOVED = manapi::net::worker::base::CONN_REMOVED,
    HTTP2_STREAM_RECV_END = manapi::net::worker::base::CONN_RECV_END,
    HTTP2_STREAM_SEND_END  = manapi::net::worker::base::CONN_SEND_END,
    HTTP2_STREAM_IO_WAITING = manapi::net::worker::base::CONN_IO_WAITING,
    HTTP2_STREAM_TOP_READ = manapi::net::worker::base::CONN_TOP_READ
    //HTTP2_STREAM_START_WORK_WAIT = 2048
};

struct manapi::net::worker::ng_wrk_http2_ctx_global_t {
    std::shared_ptr<net::worker::http_v2> worker;
    net::worker::base *base_worker;
};

struct http_v2_stream_t : manapi::net::worker::http_v2_stream_base_t {
    uint32_t id;
    std::unique_ptr<manapi::net::http::request_data_t> req;
};

struct nghttp2_session_deleter {
    void operator()(nghttp2_session *s) {
        nghttp2_session_del(s);
    }
};

struct nghttp2_session_callbacks_deleter {
    void operator()(nghttp2_session_callbacks *c) {
        nghttp2_session_callbacks_del(c);
    }
};

struct manapi::net::worker::ng_wrk_http2_ctx_t {
    ng_wrk_http2_ctx_global_t *gctx;
    char flgs;
    std::unique_ptr<nghttp2_session, nghttp2_session_deleter> ctx;
    shared_conn conn;
    std::map<uint32_t, shared_conn> streams;
};

int ng_wrk_http2_cleanup (manapi::net::worker::connection *conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto wrk_data = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(conn->wrk.data);
    delete wrk_data;
    conn->wrk.flags = 0;
    conn->wrk.data = nullptr;
    return manapi::ERR_OK;
}

int ng_wrk_http2_write (nghttp2_session *s) {
    if (int const rhs = nghttp2_session_send (s)) {
        MANAPIHTTP_LOG("nghttp2: {}", nghttp2_strerror(rhs));
        return manapi::ERR_ABORTED;
    }
    return manapi::ERR_OK;
}

int ng_wrk_http2(const manapi::net::worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto wrk_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    auto http_v2_ctx = wrk_ctx->ctx.get();

    if (flags & manapi::ev::DISCONNECT) {
        goto err;
    }

    if (flags & manapi::ev::WRITE) {
        if (!nghttp2_session_want_read(http_v2_ctx)
            && !nghttp2_session_want_write(http_v2_ctx)) {
            goto err;
        }

        if (ng_wrk_http2_write(http_v2_ctx))
            goto err;
    }

    if (flags & manapi::ev::READ) {
        ssize_t res = 0;
        while (res != nsize) {
            auto rhs = nghttp2_session_mem_recv(http_v2_ctx, reinterpret_cast<const uint8_t *> (buffer) + res, nsize - res);
            if (rhs < 0)
                goto err;

            res += rhs;

            if (ng_wrk_http2_write(http_v2_ctx))
                goto err;
        }
    }

    return manapi::ERR_OK;

    err: {

        return manapi::ERR_UNKNOWN;
    }
}

ssize_t ng_wrk_http2_send_callback(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data) {
    auto const s = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);

    auto const rhs = s->gctx->base_worker->sync_write(s->conn, data,
        static_cast<ssize_t>(length), false);

    if (rhs > 0)
        return rhs;

    if (rhs == 0)
        return NGHTTP2_ERR_WOULDBLOCK;

    return NGHTTP2_ERR_CALLBACK_FAILURE;
}

int ng_wrk_http2_on_frame_recv_callback (nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    auto const s = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    switch (frame->hd.type) {
        case NGHTTP2_DATA: {
            auto s = static_cast<http_v2_stream_t *>(nghttp2_session_get_stream_user_data(session, frame->hd.stream_id));
            if (!s)
                return 0;

            break;
        }
        case NGHTTP2_PRIORITY: {
            break;
        }
        case NGHTTP2_RST_STREAM: {
            break;
        }
        case NGHTTP2_SETTINGS: {
            break;
        }
        case NGHTTP2_PING: {
            break;
        }
        case NGHTTP2_GOAWAY: {
            break;
        }
        case NGHTTP2_ALTSVC: {
            break;
        }
        case NGHTTP2_ORIGIN: {
            break;
        }
        case NGHTTP2_PRIORITY_UPDATE: {
            break;
        }
        default:
            break;
    }
    return 0;
}

int ng_wrk_http2_on_stream_close_callback (nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *user_data) {
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    auto s = static_cast<http_v2_stream_t *>(nghttp2_session_get_stream_user_data(session, stream_id));
    if (!s)
        return 0;
    nghttp2_session_set_stream_user_data(sess->ctx.get(), stream_id, nullptr);
    sess->streams.erase(stream_id);
    // delete stream
    return 0;
}

int ng_wrk_http2_on_header_callback (nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags, void *user_data) {
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    switch (frame->hd.type) {
        case NGHTTP2_HEADERS: {
            if (frame->headers.cat != NGHTTP2_HCAT_REQUEST)
                break;

            auto s = static_cast<http_v2_stream_t *>(nghttp2_session_get_stream_user_data(session, frame->hd.stream_id));
            if (!s)
                break;


            s->req->headers.insert({std::string(reinterpret_cast<const char *>(name), namelen),
                std::string(reinterpret_cast<const char *>(value), valuelen)});

            break;
        }
        default:
            break;
    }
    return 0;
}

int ng_wrk_http2_data_chunk_recv_callback (nghttp2_session *session, uint8_t flags, int32_t stream_id, const uint8_t *data, size_t len, void *user_data) {
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    auto s = static_cast<http_v2_stream_t *>(nghttp2_session_get_stream_user_data(session, stream_id));
    if (!s)
        return NGHTTP2_ERR_STREAM_CLOSED;

    auto const config = sess->gctx->base_worker->config();

    if (s->recv_size < 1e5)
        return NGHTTP2_ERR_PAUSE;

    auto rhs = manapi::net::worker::base::connection_io_send(s->recv.get(), reinterpret_cast<const char*>(data), len, &sess->gctx->base_worker->bufferpool(),
        config->buffer_size, &s->recv_size, 1e5);

    return rhs;
}

void ng_wrk_http2_stream_deleter (manapi::net::worker::connection *w) {
    std::unique_ptr<manapi::net::worker::connection> s (w);
    auto const data = w->as<http_v2_stream_t>();

    delete data;
}

int ng_wrk_http2_on_begin_headers_callback(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    if (frame->hd.type != NGHTTP2_HEADERS ||
                frame->headers.cat != NGHTTP2_HCAT_REQUEST)
        return 0;
    // create connection

    auto const id = frame->hd.stream_id;

    auto tp = std::make_unique<http_v2_stream_t>();
    tp->speed_min_delay = static_cast<decltype(tp->speed_min_delay)>(sess->gctx->base_worker->config()->speed_check_delay);
    tp->id = id;

    std::shared_ptr<manapi::net::worker::connection> w(new manapi::net::worker::connection(tp.release()),
        ng_wrk_http2_stream_deleter);

    auto const pointer = tp.get();

    w->wrk.data = sess;

    if (!sess->streams.insert({id, std::move(w)}).second)
        /* failed to insert */
        return NGHTTP2_ERR_DATA_EXIST;

    if (auto const rhs = nghttp2_session_set_stream_user_data (session, id, pointer))
        return rhs;

    return 0;
}

int ng_wrk_http2_init (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    try {
        assert(!conn->wrk.data);

        auto tp = std::make_unique<manapi::net::worker::ng_wrk_http2_ctx_t>();

        /* nghttp2 init */
        nghttp2_session_callbacks *callbacks;
        if (nghttp2_session_callbacks_new(&callbacks))
            return manapi::ERR_RESOURCE_EXHAUSTED;

        std::unique_ptr<nghttp2_session_callbacks, nghttp2_session_callbacks_deleter> callbacks_s;
        callbacks_s.reset(callbacks);


        nghttp2_session_callbacks_set_send_callback(callbacks, ng_wrk_http2_send_callback);

        //nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, ng_wrk_http2_on_frame_recv_callback);

        nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, ng_wrk_http2_on_stream_close_callback);

        nghttp2_session_callbacks_set_on_header_callback(callbacks, ng_wrk_http2_on_header_callback);

        nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, ng_wrk_http2_on_begin_headers_callback);

        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, ng_wrk_http2_data_chunk_recv_callback);


        nghttp2_session *s;
        nghttp2_session_server_new(&s, callbacks, tp.get());
        tp->ctx.reset(s);

        tp->conn = conn;
        conn->wrk.data = tp.release();

        conn->wrk.flags |= manapi::net::worker::WRK_INTERFACE_CUSTOM_RATE_LIMIT;

        auto wrk_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
        auto global_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_global_t *> (global->data);

        wrk_ctx->gctx = global_ctx;

        if (auto rhs = ng_wrk_http2 (conn, manapi::ev::READ, NGHTTP2_CLIENT_MAGIC, 14, nullptr, global, w)) {
            return manapi::ERR_UNKNOWN;
        }

        w->event_on(conn, std::make_unique<manapi::net::worker::worker_watcher_cb>(
            [w, global]
            (const manapi::net::worker::shared_conn & conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p)
            -> void {
            ng_wrk_http2(conn, flags, buffer, nsize, p, global, w);
        }));

        w->event_flags(conn, manapi::ev::READ|manapi::ev::WRITE);

        return manapi::ERR_OK;
    }
    catch (...) {
        return manapi::ERR_UNKNOWN;
    }
}

bool ng_wrk_http2_update_limit_rate (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    if (http_v2_ctx) {
        for (auto const &s : http_v2_ctx->streams) {
            static_cast<manapi::net::worker::ng_wrk_http2_ctx_global_t *> (global->data)
                ->worker->update_limit_rate_stream(s.second);
        }
    }
    return false;
}

int ng_wrk_http2_global_cleanup (manapi::net::worker::wrk_interface_global_t *data, manapi::net::worker::base *w) {
    auto ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_global_t *> (data->data);
    delete ctx;
    data->data = nullptr;
    return manapi::ERR_OK;
}

struct nghttp2_nv_deleter {
    void operator()(nghttp2_nv *p) {
        delete[] p;
    }
};

ssize_t ng_wrk_http2_read_cb (nghttp2_session *session, int32_t stream_id, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void *user_data) {
    auto s = static_cast<http_v2_stream_t *>(nghttp2_session_get_stream_user_data(session, stream_id));
    if (!s)
        return 0;

    ssize_t res = 0;

    if (s->flags & HTTP2_STREAM_SEND_END)
        (*data_flags) |= NGHTTP2_DATA_FLAG_EOF;
    else if (!s->send_size)
        return NGHTTP2_ERR_DEFERRED;

    auto top = s->send.get();

    while (res < length && s->send_size) {
        ssize_t buffer_size;
        if (top->deque.get() == top->last_deque)
            buffer_size = top->deque_cursor;
        else
            buffer_size = top->deque->buffer.size();

        auto const copy = std::min<ssize_t>(length - res, buffer_size - top->deque_current);
        memcpy (buf + res, top->deque->buffer.data() + top->deque_current, copy);

        top->deque_current += copy;
        res += copy;

        if (top->deque_current == buffer_size) {
            top->deque = std::move(top->deque->next);
            top->deque_current = 0;
            s->send_size--;
            if (!top->deque) {
                top->last_deque = nullptr;
                top->deque_cursor = 0;
            }
        }
    }

    return res;
}

manapi::future<ssize_t> ng_wrk_http2_send_response (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) {
    auto const s = conn->as<http_v2_stream_t>();
    if (!s)
        co_return -1;

    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    auto &headers = res->headers();
    std::unique_ptr<nghttp2_nv, nghttp2_nv_deleter> p (new nghttp2_nv[headers.size()]);
    std::size_t i = 0;
    auto nvs = p.get();
    for (auto &[h, v] : headers) {
        auto &nv = nvs[i++];
        nv.name = (uint8_t*)h.data();
        nv.namelen = h.size();
        nv.value = reinterpret_cast<uint8_t*>(v.data());
        nv.valuelen = v.size();
        nv.flags = NGHTTP2_NV_FLAG_NO_COPY_NAME|NGHTTP2_NV_FLAG_NO_COPY_VALUE;
    }

    nghttp2_data_provider pr;
    pr.read_callback = ng_wrk_http2_read_cb;

    auto const rhs = nghttp2_submit_response(http_v2_ctx->ctx.get(), s->id, nvs, headers.size(), &pr);
    if (rhs) {
        MANAPIHTTP_LOG("nghttp2: failed to send headers due to {}", nghttp2_strerror(rhs));
        co_return -1;
    }


    co_return 1;
}

manapi::error::status manapi::net::worker::ng_wrk_http2_global_init(manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    try {
        assert(!global->data);

        auto tp = std::make_unique<ng_wrk_http2_ctx_global_t>();

        tp->worker = std::make_shared<worker::http_v2>(w, &ng_wrk_http2_callbacks);
        tp->worker->init();

        tp->base_worker = w;

        global->data = tp.release();

        global->accept_cb = ng_wrk_http2;
        global->init_cb = ng_wrk_http2_init;
        global->cleanup_cb = ng_wrk_http2_cleanup;
        global->cleanup_global_cb = ng_wrk_http2_global_cleanup;
        global->flush_custom_read_cb = nullptr;
        global->update_limit_rate = ng_wrk_http2_update_limit_rate;
        global->custom_read_cb = nullptr;
        global->send_response = ng_wrk_http2_send_response;

        return error::status_ok();
    }
    catch (std::bad_alloc const &e) {
        return manapi::error::status_resource_exhausted("nghttp2: bad_alloc");
    }
    catch (...) {
        return manapi::error::status_unknown("nghttp2: got something wrong");
    }
}

bool ng_wrk_http2_is_writable (const manapi::net::worker::shared_conn &conn) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    auto const s = conn->as<http_v2_stream_t>();
    auto const config = http_v2_ctx->gctx->base_worker->config();
    if (s->send_size < config->max_buffer_stack)
        return false;

    return true;
}

int ng_wrk_http2_want_write (const manapi::net::worker::shared_conn &conn) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    http_v2_ctx->gctx->base_worker->event_toggle(http_v2_ctx->conn, true, manapi::ev::WRITE);
    return 0;
}

manapi::net::worker::connection::ipdata_t * ng_wrk_http2_ipdata (manapi::net::worker::connection *conn) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    return http_v2_ctx->gctx->base_worker->ipdata(http_v2_ctx->conn.get());
}

int ng_wrk_http2_rst (const manapi::net::worker::shared_conn &conn, int code) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    auto const s = conn->as<http_v2_stream_t>();
    return nghttp2_submit_rst_stream(http_v2_ctx->ctx.get(), 0, s->id, code);
}

int ng_wrk_http2_on_read_stream (const manapi::net::worker::shared_conn &conn) {
    auto const s = conn->as<http_v2_stream_t>();
    if (auto const rhs = manapi::net::worker::http_v2_flush_recv (conn, s))
        return rhs;

    return manapi::ERR_OK;
}

ssize_t ng_wrk_http2_write (const manapi::net::worker::shared_conn &conn, manapi::ev::buff_t *buff, uint32_t nbuff, bool finish) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    auto const s = conn->as<http_v2_stream_t>();

    if (s->flags & (HTTP2_STREAM_CLOSED))
        return -1;

    auto config = http_v2_ctx->gctx->base_worker->config();
    auto bufferpool = &http_v2_ctx->gctx->base_worker->bufferpool();
    ssize_t res = 0;

    while (nbuff && s->send_size <= config->max_buffer_stack) {
        auto const rhs = manapi::net::worker::base::connection_io_send(s->send.get(), buff->base, buff->len, bufferpool, config->buffer_size,
            &s->send_size, config->max_buffer_stack);

        if (!rhs)
            break;

        buff->len -= rhs;
        res += rhs;

        if (!buff->len) {
            buff++;
            nbuff--;
        }

        if (const auto err = nghttp2_session_resume_data(http_v2_ctx->ctx.get(), s->id)) {
            MANAPIHTTP_LOG("nghttp2: resume data send failed due to {}", nghttp2_strerror(err));
            return -1;
        }
    }

    return res;
}

manapi::net::worker::http_v2_callbacks_t ng_wrk_http2_callbacks {
    .http_v2_write = ng_wrk_http2_write,
    .http_v2_on_read_stream = ng_wrk_http2_on_read_stream,
    .http_v2_want_write = ng_wrk_http2_want_write,
    .http_v2_rst_stream = ng_wrk_http2_rst,
    .http_v2_is_writable = ng_wrk_http2_is_writable,
    .http_v2_ip_data = ng_wrk_http2_ipdata
};