#include "../include/worker/ManapiNgHttp2Interface.hpp"

#include "nghttp2/nghttp2.h"
#include "nghttp2/nghttp2ver.h"

extern manapi::net::worker::http_v2_callbacks_t ng_wrk_http2_callbacks;

struct manapi::net::worker::ng_wrk_http2_ctx_global_t {
    std::shared_ptr<net::worker::http_v2> worker;
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
};

int ng_wrk_http2_cleanup (manapi::net::worker::connection *conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto wrk_data = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(conn->wrk.data);
    delete wrk_data;
    conn->wrk.flags = 0;
    conn->wrk.data = nullptr;
    return manapi::ERR_OK;
}

int ng_wrk_http2(const manapi::net::worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto wrk_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    auto http_v2_ctx = wrk_ctx->ctx.get();

    int rhs;

    if (flags & manapi::ev::DISCONNECT) {
        goto err;
    }

    if (flags & manapi::ev::WRITE) {

    }

    if (flags & manapi::ev::READ) {
    }

    return manapi::ERR_OK;

    err: {

        return manapi::ERR_UNKNOWN;
    }
}

ssize_t ng_wrk_http2_send_callback(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data) {
    manapi::ev::buff_t buff;
    buff.base = (char*)data;
    buff.len = length;
    //return
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

        // nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
        //                                                      on_frame_recv_callback);
        //
        // nghttp2_session_callbacks_set_on_stream_close_callback(
        //     callbacks, on_stream_close_callback);
        //
        // nghttp2_session_callbacks_set_on_header_callback(callbacks,
        //                                                  on_header_callback);
        //
        // nghttp2_session_callbacks_set_on_begin_headers_callback(
        //     callbacks, on_begin_headers_callback);


        nghttp2_session *s;
        nghttp2_session_server_new(&s, callbacks, tp.get());
        tp->ctx.reset(s);

        tp->conn = conn;
        conn->wrk.data = tp.release();

        conn->wrk.flags |= manapi::net::worker::WRK_INTERFACE_CUSTOM_RATE_LIMIT;

        auto wrk_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
        auto global_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_global_t *> (global->data);

        wrk_ctx->gctx = global_ctx;

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
    // auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    // if (http_v2_ctx->ctx->streams) {
    //     for (const auto &s : *http_v2_ctx->ctx->streams) {
    //         static_cast<manapi::net::worker::ng_wrk_http2_ctx_global_t *> (global->data)
    //             ->worker->update_limit_rate_stream(s.second);
    //     }
    // }
    return false;
}

int ng_wrk_http2_global_cleanup (manapi::net::worker::wrk_interface_global_t *data, manapi::net::worker::base *w) {
    auto ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_global_t *> (data->data);
    delete ctx;
    data->data = nullptr;
    return manapi::ERR_OK;
}

manapi::future<ssize_t> ng_wrk_http2_send_response (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global,
    manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) {

}

manapi::error::status manapi::net::worker::ng_wrk_http2_global_init(manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    assert(!global->data);

    auto tp = std::make_unique<ng_wrk_http2_ctx_global_t>();

    tp->worker = std::make_shared<worker::http_v2>(w, &ng_wrk_http2_callbacks);
    tp->worker->init();

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

bool ng_wrk_http2_is_writable (const manapi::net::worker::shared_conn &conn) {


    return true;
}

int ng_wrk_http2_want_write (const manapi::net::worker::shared_conn &conn) {
    //auto s = conn->as<manapi::net::http::http_v2_stream_t>();
    //s->ctx->worker->event_toggle(s->ctx->conn, manapi::ev::WRITE, true);
    return 0;
}

manapi::net::worker::connection::ipdata_t * ng_wrk_http2_ipdata (manapi::net::worker::connection *conn) {
    //auto s = conn->as<manapi::net::http::http_v2_stream_t>();
    //return s->ctx->worker->ipdata(s->ctx->conn.get());
}

int ng_wrk_http2_rst (const manapi::net::worker::shared_conn &conn, int code) {

}

int ng_wrk_http2_on_read_stream (const manapi::net::worker::shared_conn &conn) {

}

ssize_t ng_wrk_http2_write (const manapi::net::worker::shared_conn &conn, manapi::ev::buff_t *buff, uint32_t nbuff, bool finish) {

}

manapi::net::worker::http_v2_callbacks_t ng_wrk_http2_callbacks {
    .http_v2_write = ng_wrk_http2_write,
    .http_v2_on_read_stream = ng_wrk_http2_on_read_stream,
    .http_v2_want_write = ng_wrk_http2_want_write,
    .http_v2_rst_stream = ng_wrk_http2_rst,
    .http_v2_is_writable = ng_wrk_http2_is_writable,
    .http_v2_ip_data = ng_wrk_http2_ipdata
};