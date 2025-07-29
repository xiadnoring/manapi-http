#include "../include/http/ManapiNgHttp3Interface.hpp"

#if MANAPIHTTP_NGHTTP3_DEPENDENCY

#   include <cstring>

#   include "nghttp3/nghttp3.h"
#   include "nghttp3/version.h"

#   include "ManapiHttpResponse.hpp"
#   include "components/ManapiURLDecodeStream.hpp"
#   include "http/ManapiBaseHttp.hpp"
#   include "../include/ManapiSiteInternal.hpp"

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
    HTTP3_STREAM_IS_READING = manapi::net::worker::base::CONN_MAX_CODE * 2
    //HTTP3_STREAM_START_WORK_WAIT = 2048
};

struct manapi_nghttp3_conn_deleter {
    void operator () (nghttp3_conn *conn) const MANAPIHTTP_NOEXPECT {
        nghttp3_conn_del(conn);
    }
};

struct manapi::net::worker::ng_wrk_http3_ctx_global_t {
    std::shared_ptr<net::worker::http_v3> http3;
    net::worker::base *worker;
};

struct manapi::net::worker::ng_wrk_http3_ctx_t {
    shared_conn conn;
    nghttp3_settings h3_settings;
    std::unique_ptr<nghttp3_conn, manapi_nghttp3_conn_deleter> ctx;
    std::unique_ptr<const nghttp3_mem> h3_mem;
    ng_wrk_http3_ctx_global_t *gctx;
};

struct ng_wrk_http3_stream_t : manapi::net::worker::http_v3_stream_base_t {
    manapi::net::worker::ng_wrk_http3_ctx_t *ctx;
    manapi::net::worker::shared_conn s;

    manapi::ev::buff_t *buffs;
    ssize_t size;
};

struct nghttp3_nv_deleter {
    void operator () (nghttp3_nv *src) const MANAPIHTTP_NOEXPECT {
        delete[] src;
    }
};

#   define MANAPI_AS_STREAM(n__) (static_cast<ng_wrk_http3_stream_t *>(n__))
#   define MANAPI_AS_CONN(n__) (static_cast<manapi::net::worker::ng_wrk_http3_ctx_t *>(n__))

static int ng_wrk_http3_flush_write (manapi::net::worker::ng_wrk_http3_ctx_t *ctx) MANAPIHTTP_NOEXPECT {
    int64_t v_stream_id;
    nghttp3_vec vec[8];

    while (true) {
        int pfin = 0;
        bool fin;

        auto vec_len = nghttp3_conn_writev_stream(ctx->ctx.get(), &v_stream_id, &pfin, vec, sizeof (vec) / sizeof (nghttp3_vec));

        if (vec_len < 0) {
            manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s: %s failed due to %s", "nghttp3",
                "nghttp3_conn_writev_stream", nghttp3_strerror(vec_len));
            break;
        }

        if (v_stream_id < 0)
            break;


        auto v_conn = ctx->gctx->worker->stream_id(ctx->conn, v_stream_id);

        assert (v_conn && v_conn->wrk.data);

        fin = pfin & NGHTTP3_DATA_FLAG_EOF;

        auto v_s = MANAPI_AS_STREAM(v_conn->wrk.data);

        if (vec_len) {
            for (nghttp3_ssize i = 0; i < vec_len; i++) {
                auto &b = vec[i];
                auto rhs = ctx->gctx->worker->sync_write_ex(v_conn, b.base,
                    b.len, fin && i + 1 == vec_len, 1e5);
                if (rhs != b.len)
                    goto err;
            }
        }
        else if (fin) {
            if (ctx->gctx->worker->sync_write(v_conn, static_cast<char*>(nullptr), 0, true))
                goto err;
        }

        continue;

        err: {
            ctx->gctx->http3->close_connection(v_conn, manapi::net::worker::CLOSE_CONN_ERR);
        }
    }

    return manapi::ERR_OK;
}

static ssize_t ng_wrk_http3_write (const manapi::net::worker::shared_conn &conn, manapi::ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXPECT {
    auto const s = MANAPI_AS_STREAM (conn->wrk.data);
    auto const ctx = s->ctx->ctx.get();
    auto const stream_id = static_cast<int64_t>(s->ctx->gctx->worker->stream_id(conn));

    if (s->flags & (HTTP3_STREAM_CLOSED))
        return -manapi::ERR_ABORTED;

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
        s->ctx->gctx->http3->close_connection(s->s, manapi::net::worker::CLOSE_CONN_ERR);
        return -manapi::ERR_INTERNAL;
    }
}

static int ng_wrk_http3_global_cleanup (manapi::net::worker::wrk_interface_global_t *data, manapi::net::worker::base *w) MANAPIHTTP_NOEXPECT {
    auto ctx = static_cast<manapi::net::worker::ng_wrk_http3_ctx_global_t *> (data->data);
    delete ctx;
    data->data = nullptr;
    return manapi::ERR_OK;
}

static int ng_wrk_http3_cleanup (manapi::net::worker::connection *conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXPECT {
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

static int ng_wrk_http3(const manapi::net::worker::shared_conn &stream, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXPECT {
    auto s = static_cast<ng_wrk_http3_stream_t *> (stream->wrk.data);

    assert (s);

    int64_t stream_id = s->ctx->gctx->worker->stream_id(s->s);

    if (flags & manapi::ev::DISCONNECT)
        goto err;

    if (flags & manapi::ev::WRITE) {
        assert(s->top->send.deque);
        s->ctx->gctx->http3->feed_event(stream, manapi::ev::WRITE, nullptr, 0, nullptr);
        if (!s->top->send.deque) {
            s->ctx->gctx->worker->event_toggle(stream, false, manapi::ev::WRITE);
        }
    }

    if (flags & manapi::net::worker::base::CONN_READ) {
        ssize_t res = 0;

        while (res != nsize) {
            auto rhs = nghttp3_conn_read_stream (s->ctx->ctx.get(), stream_id, reinterpret_cast<const uint8_t *>(buffer) + res,
                nsize - res, flags & manapi::net::worker::base::CONN_RECV_END);

            if (rhs < 0)
                goto err;

            if (!rhs) {
                s->ctx->gctx->worker->event_toggle(stream, false, manapi::ev::READ);
                s->ctx->gctx->worker->feed_event(stream, manapi::net::worker::base::CONN_TOP_READ, buffer + res, nsize - res, p);
                break;
            }

            res += rhs;
        }
    }
    else if (flags & (manapi::net::worker::base::CONN_RECV_END)) {
        auto rhs = nghttp3_conn_read_stream (s->ctx->ctx.get(), stream_id, reinterpret_cast<const uint8_t *>(buffer),
                0, flags & manapi::net::worker::base::CONN_RECV_END);

        if (rhs)
            goto err;
    }

    return manapi::ERR_OK;

    err: {
        s->ctx->gctx->http3->close_connection(stream, manapi::net::worker::CLOSE_CONN_ERR);
        return manapi::ERR_INTERNAL;
    }
}

static int ng_wrk_http3_stream_init (const manapi::net::worker::shared_conn &conn, const manapi::net::worker::shared_conn &stream, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXPECT {
    assert (conn->wrk.data);
    assert (!stream->wrk.data);
    assert (!(conn->wrk.flags & manapi::net::worker::WRK_INTERFACE_IS_STREAM));
    assert (stream->wrk.flags & manapi::net::worker::WRK_INTERFACE_IS_STREAM);

    std::unique_ptr<ng_wrk_http3_stream_t> tp (new (std::nothrow) ng_wrk_http3_stream_t{});

    if (!tp)
        return manapi::ERR_RESOURCE_EXHAUSTED;

    tp->ctx = static_cast<manapi::net::worker::ng_wrk_http3_ctx_t *> (conn->wrk.data);
    tp->top.reset(new (std::nothrow) manapi::net::worker::connection_io{});
    tp->s = stream;

    if (!tp->top)
        return manapi::ERR_RESOURCE_EXHAUSTED;

    try {
        stream->wrk.data = tp.release();

        w->event_on(stream, std::make_unique<manapi::net::worker::worker_watcher_cb>(
            [w, global]
            (const manapi::net::worker::shared_conn & conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p)
            -> void {
            ng_wrk_http3(conn, flags, buffer, nsize, p, global, w);
        }));

        w->event_flags(stream, manapi::ev::READ);
    }
    catch (std::bad_alloc const &) {
        return manapi::ERR_RESOURCE_EXHAUSTED;
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return manapi::ERR_INTERNAL;
    }

    return manapi::ERR_OK;
}

static int ng_wrk_http3_init (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXPECT {
    try {
        assert(!conn->wrk.data);

        assert (!(conn->wrk.flags & manapi::net::worker::WRK_INTERFACE_IS_STREAM));

        auto tp = std::make_unique<manapi::net::worker::ng_wrk_http3_ctx_t>();

        /* nghttp2 init */
        nghttp3_callbacks callbacks;

        // nghttp2_session_callbacks_set_send_callback(callbacks, ng_wrk_http2_send_callback);
        //
        // nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, ng_wrk_http2_on_frame_recv_callback);
        //
        // nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, ng_wrk_http2_on_stream_close_callback);
        //
        // nghttp2_session_callbacks_set_on_header_callback(callbacks, ng_wrk_http2_on_header_callback);
        //
        // nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, ng_wrk_http2_on_begin_headers_callback);
        //
        // nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, ng_wrk_http2_data_chunk_recv_callback);

        auto global_ctx = static_cast<manapi::net::worker::ng_wrk_http3_ctx_global_t *> (global->data);
        tp->gctx = global_ctx;

        auto const config = tp->gctx->worker->config();

        nghttp3_settings_default(&tp->h3_settings);

        tp->h3_settings.max_field_section_size = config->max_headers_size;
        tp->h3_settings.qpack_max_dtable_capacity = config->max_hpack_table_size;
        tp->h3_settings.qpack_encoder_max_dtable_capacity = config->max_hpack_table_size;

        tp->h3_mem.reset(nghttp3_mem_default());
        if (!tp->h3_mem)
            return manapi::ERR_RESOURCE_EXHAUSTED;

        nghttp3_conn *p;
        auto rhs = nghttp3_conn_server_new(&p, &callbacks, &tp->h3_settings, tp->h3_mem.get(), tp.get());
        if (rhs) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH,
                "nghttp3_conn_server_new failed %s", nghttp3_strerror(rhs));
            return manapi::ERR_INTERNAL;
        }

        tp->ctx.reset(p);

        tp->conn = conn;

        if (config->max_concurrent_streams > 0)
            nghttp3_conn_set_max_concurrent_streams(tp->ctx.get(), config->max_concurrent_streams);

        conn->wrk.data = tp.release();

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
    ssize_t res = 0;
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

    if (buffs) {
        while (veccnt && s->size) {
            vec->base = reinterpret_cast<uint8_t *>(buffs->base);
            vec->len = buffs->len;

            s->size += res;

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

    auto const rhs = nghttp3_conn_submit_response(ctx->ctx.get(), ctx->gctx->worker->stream_id(stream),
        p, hs, &dr);

    if (rhs) {
        MANAPIHTTP_LOG("nghttp2: failed to send headers due to {}", nghttp3_strerror(rhs));
        return manapi::ERR_INTERNAL;
    }

    return manapi::ERR_OK;
}

static manapi::future<int> ng_wrk_http3_send_response (const manapi::net::worker::shared_conn &stream, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) {
    co_return ng_wrk_http3_send_response_sync(stream, global, w, res, finish);
}

manapi::error::status manapi::net::worker::ng_wrk_http3_global_init(manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::interface_worker *w) MANAPIHTTP_NOEXPECT {
    try {
        assert(!global->data);

        if (!(w->worker_flags() & WORKER_BASE_FLAG_MULTISTREAM))
            return error::status_invalid_argument("worker doesn't support multistream");


        auto tp = std::make_unique<ng_wrk_http3_ctx_global_t>();

        tp->http3 = std::make_shared<worker::http_v3>(w, &ng_wrk_http3_callbacks);
        // tp->worker->init(0);
        tp->worker = w;

        global->flags |= WRK_GLOBAL_FLAG_MULTISTREAM;
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

manapi::net::worker::http_v3_callbacks_t ng_wrk_http3_callbacks {
    .http_v3_write = ng_wrk_http3_write,
    // .http_v3_on_read_stream = ng_wrk_http3_on_read_stream,
    // .http_v3_want_write = ng_wrk_http3_want_write,
    // .http_v3_rst_stream = ng_wrk_http3_rst,
    // .http_v3_is_writable = ng_wrk_http3_is_writable,
    // .http_v3_ip_data = ng_wrk_http3_ipdata
};

#endif