#include "../include/worker/ManapiNgHttp2Interface.hpp"
#include "../include/ManapiUtils.hpp"

#if MANAPIHTTP_NGHTTP2_DEPENDENCY

#include <cstring>

#include "nghttp2/nghttp2.h"
#include "nghttp2/nghttp2ver.h"

#include "ManapiHttpResponse.hpp"
#include "components/ManapiURLDecodeStream.hpp"
#include "http/ManapiBaseHttp.hpp"
#include "../include/ManapiSiteInternal.hpp"


extern manapi::net::worker::http_v2_callbacks_t ng_wrk_http2_callbacks;

enum http2_stream_flags {
    HTTP2_STREAM_WANT_READ = manapi::ev::READ,
    HTTP2_STREAM_WANT_WRITE = manapi::ev::WRITE,
    HTTP2_STREAM_CLOSED = manapi::ev::DISCONNECT,
    HTTP2_STREAM_REMOVED = manapi::net::worker::base::CONN_REMOVED,
    HTTP2_STREAM_RECV_END = manapi::net::worker::base::CONN_RECV_END,
    HTTP2_STREAM_SEND_END  = manapi::net::worker::base::CONN_SEND_END,
    HTTP2_STREAM_IO_WAITING = manapi::net::worker::base::CONN_IO_WAITING,
    HTTP2_STREAM_TOP_READ = manapi::net::worker::base::CONN_TOP_READ,
    HTTP2_STREAM_IS_READING = 256
    //HTTP2_STREAM_START_WORK_WAIT = 2048
};

enum http2_stream_ctx_flags {
    HTTP2_CTX_WANT_CLOSE = 1
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
    uint32_t want_read;

    manapi::ev::buff_t *buffs;
    ssize_t size;
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
        if (rhs != NGHTTP2_ERR_WOULDBLOCK) {
            MANAPIHTTP_LOG("nghttp2: {}", nghttp2_strerror(rhs));
            return manapi::ERR_ABORTED;
        }
    }
    return manapi::ERR_OK;
}

void ng_wrk_http2_on_close (const manapi::net::worker::shared_conn &conn) {
    auto wrk_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    if (!wrk_ctx)
        return;

    if (wrk_ctx->streams.empty()
        && wrk_ctx->flgs & HTTP2_CTX_WANT_CLOSE) {
        wrk_ctx->ctx.reset();
        wrk_ctx->conn.reset();
    }
}

void ng_wrk_http2_rst_streams (manapi::net::worker::ng_wrk_http2_ctx_t *ctx) {
    for (const auto &s : ctx->streams) {
        ctx->gctx->worker->close_connection(s.second, manapi::net::worker::CLOSE_CONN_ERR);
    }
}

int ng_wrk_http2_on_stream_close_callback (nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *user_data) {
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    auto conn = static_cast<manapi::net::worker::shared_conn *>(nghttp2_session_get_stream_user_data(session, stream_id));
    if (!conn)
        return 0;

    if (nghttp2_session_set_stream_user_data(sess->ctx.get(), stream_id, nullptr))
        return manapi::ERR_INVALID_ARGUMENT;

    // delete stream
    return 0;
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

        if (wrk_ctx->want_read)
            wrk_ctx->gctx->base_worker->event_toggle(wrk_ctx->conn, false, manapi::ev::READ);
    }

    return manapi::ERR_OK;

    err: {
        if (!(wrk_ctx->flgs & HTTP2_CTX_WANT_CLOSE)) {
            wrk_ctx->flgs |= HTTP2_CTX_WANT_CLOSE;
            ng_wrk_http2_rst_streams(wrk_ctx);
            ng_wrk_http2_on_close(conn);
        }
        return manapi::ERR_UNKNOWN;
    }
}

ssize_t ng_wrk_http2_send_callback(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data) {
    auto const s = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);

    auto const rhs = s->gctx->base_worker->sync_write(s->conn, data,
        static_cast<ssize_t>(length), true);

    if (rhs > 0)
        return rhs;

    if (rhs == 0)
        return NGHTTP2_ERR_WOULDBLOCK;

    return NGHTTP2_ERR_CALLBACK_FAILURE;
}

int ng_wrk_http2_on_frame_recv_callback (nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    switch (frame->hd.type) {
        case NGHTTP2_DATA: {

            auto const conn = static_cast<manapi::net::worker::shared_conn *>(nghttp2_session_get_stream_user_data(session, frame->hd.stream_id));
            if (!conn)
                return 0;

            auto s = (*conn)->as<http_v2_stream_t>();

            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM)
                s->flags |= HTTP2_STREAM_RECV_END;

            break;
        }
        case NGHTTP2_HEADERS: {
            auto const conn = static_cast<manapi::net::worker::shared_conn *>(nghttp2_session_get_stream_user_data(session, frame->hd.stream_id));
            if (!conn)
                return 0;

            auto s = (*conn)->as<http_v2_stream_t>();

            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM)
                s->flags |= HTTP2_STREAM_RECV_END;

            if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
                sess->gctx->base_worker->waiting(sess->conn, false);

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
                    catch (...) {
                        s->req->body_size = -1;
                    }

                    if (s->req->body_size < 0) {
                        status = manapi::net::http::BAD_REQUEST_400;
                        s->req->body_size = 0;
                    }
                }

                manapi::async::current()->etaskpool()->append_task(
                    [status, conn = sess->conn, id = frame->hd.stream_id, w = sess->gctx->base_worker, w2 = sess->gctx->worker] () -> void {
                        auto wrk_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
                        if (!wrk_ctx)
                            return;
                        auto http_v2_ctx = wrk_ctx->ctx.get();
                        auto s = wrk_ctx->streams.find(id);
                        if (s == wrk_ctx->streams.end())
                            return;

                        auto const sdata = s->second->as<http_v2_stream_t>();
                        auto const req_ptr = sdata->req.get();
                        manapi_log_trace("http2: %d stream on %.*s", s->first, req_ptr->uri.size(), req_ptr->uri.data());
                        auto cdata = std::make_unique<manapi::net::http::internal::handle_data_t>(s->second, w2,
                            req_ptr, std::make_unique<manapi::net::http::internal::cont_callback_cb_t>(
                            [w, sconn = s->second, conn, req = std::move(sdata->req)] (bool ok) mutable
                            -> void {
                                manapi::async::current()->etaskpool()->append_task(
                                    [w = std::move(w), ok, conn = std::move(conn), sconn = std::move(sconn)] () -> void {
                                        auto const sdata = sconn->as<http_v2_stream_t>();
                                        auto ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(conn->wrk.data);

                                        ctx->gctx->worker->close_connection(sconn, ok ? 0 : manapi::net::worker::CLOSE_CONN_ERR);
                                        auto it = ctx->streams.find(sdata->id);
                                        assert(it != ctx->streams.end());
                                        ng_wrk_http2_on_stream_close_callback(ctx->ctx.get(),  sdata->id, 0,
                                            ctx);
                                        ctx->streams.erase(it);

                                        //manapi::net::http::http_v2_on_close_stream(ctx->ctx.get(), sdata->id);

                                        if (ctx->streams.empty()) {
                                            w->waiting(conn, true);
                                            ng_wrk_http2_on_close (conn);
                                        }
                                });
                        }));

                        // this->event_on(conn, std::unique_ptr<worker_watcher_cb>(nullptr));
                        // this->event_flags(conn, 0);

                        cdata->router = w->site().handler(req_ptr);
                        manapi::net::http::internal::handle_income_request(std::move(cdata), status);
                });
            }

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

int ng_wrk_http2_on_header_callback (nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags, void *user_data) {
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    switch (frame->hd.type) {
        case NGHTTP2_HEADERS: {
            if (frame->headers.cat != NGHTTP2_HCAT_REQUEST)
                break;

            auto conn = static_cast<manapi::net::worker::shared_conn *>(nghttp2_session_get_stream_user_data(session, frame->hd.stream_id));
            if (!conn)
                break;

            auto s = (*conn)->as<http_v2_stream_t>();

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
    auto conn = static_cast<manapi::net::worker::shared_conn *>(nghttp2_session_get_stream_user_data(session, stream_id));
    if (!conn)
        return NGHTTP2_ERR_STREAM_CLOSED;

    auto const s = (*conn)->as<http_v2_stream_t>();

    auto const config = sess->gctx->base_worker->config();

    auto rhs = manapi::net::worker::base::connection_io_send(s->recv.get(), reinterpret_cast<const char*>(data), len, &sess->gctx->base_worker->bufferpool(),
        config->buffer_size, &s->recv_size, 1e5);

    s->transfered_k += rhs;

    if (manapi::net::worker::http_v2_flush_recv(*conn, s))
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    if (s->recv_size > config->max_buffer_stack
            && !(s->flags & HTTP2_STREAM_IS_READING)) {
        s->flags |= HTTP2_STREAM_IS_READING;
        sess->want_read ++;
    }

    return 0;
}

void ng_wrk_http2_stream_deleter (manapi::net::worker::connection *w) {
    if (!w)
        return;

    std::unique_ptr<manapi::net::worker::connection> s (w);
    auto const data = w->as<http_v2_stream_t>();

    delete data;
}

int ng_wrk_http2_on_begin_headers_callback(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    try {
        auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
        if (frame->hd.type != NGHTTP2_HEADERS ||
                    frame->headers.cat != NGHTTP2_HCAT_REQUEST)
            return 0;
        // create connection

        auto const id = frame->hd.stream_id;

        auto tp = std::make_unique<http_v2_stream_t>();
        tp->speed_min_delay = static_cast<decltype(tp->speed_min_delay)>(sess->gctx->base_worker->config()->speed_check_delay);
        tp->id = id;
        tp->recv = std::make_unique<manapi::net::worker::connection_io_part>();
        tp->req = std::make_unique<manapi::net::http::request_data_t>();

        auto const pointer = tp.get();

        std::shared_ptr<manapi::net::worker::connection> w(new manapi::net::worker::connection(tp.release()),
            ng_wrk_http2_stream_deleter);

        w->version = manapi::net::http::versions::HTTP_v2;
        pointer->req->http = w->version;
        w->wrk.data = sess;

        auto res = sess->streams.insert({id, std::move(w)});
        if (!res.second)
            /* failed to insert */
                return NGHTTP2_ERR_DATA_EXIST;

        if (auto const rhs = nghttp2_session_set_stream_user_data (session, id, &res.first->second))
            return rhs;

        return 0;
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("nghttp2: init stream failed due to {}", e.what());
    }
    return NGHTTP2_ERR_FATAL;
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

        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, ng_wrk_http2_on_frame_recv_callback);

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

        auto const config = wrk_ctx->gctx->base_worker->config();

        nghttp2_settings_entry settings[] = {
            {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, static_cast<uint32_t>(config->max_concurrent_streams >= 0 ? config->max_concurrent_streams : 100)},
            {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, static_cast<uint32_t>(config->max_hpack_list_size < 0 ? 4096 : config->max_hpack_list_size)},
            {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, static_cast<uint32_t>(config->initial_window_size < 0 ? 65535 : config->initial_window_size)},
            {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, static_cast<uint32_t>(config->max_frame_size < 0 ? 16384 : config->max_frame_size)}
        };

        if (auto rhs = nghttp2_submit_settings(s, NGHTTP2_FLAG_NONE, settings, 4)) {
            return manapi::ERR_UNKNOWN;
        }

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

void ng_wrk_http2_update_limit_rate (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    if (http_v2_ctx) {
        for (auto const &s : http_v2_ctx->streams) {
            static_cast<manapi::net::worker::ng_wrk_http2_ctx_global_t *> (global->data)
                ->worker->update_limit_rate_stream(s.second);
        }
    }
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
    auto conn = static_cast<manapi::net::worker::shared_conn *>(nghttp2_session_get_stream_user_data(session, stream_id));
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    if (!conn)
        return 0;

    auto s = (*conn)->as<http_v2_stream_t>();

    ssize_t res = 0;

    auto buffs = sess->buffs;
    sess->buffs = nullptr;

    if (!sess->size || !buffs) {

        if (s->flags & HTTP2_STREAM_SEND_END) {
            (*data_flags) |= NGHTTP2_DATA_FLAG_EOF;
            return 0;
        }

        /* TODO: speed up. Critical function (large count of the executions) */
        manapi::async::current()->etaskpool()->append_task([w = sess->gctx->worker, conn = *conn] () -> void {
            w->feed_event(conn, manapi::ev::WRITE, nullptr, 0, nullptr);
        });
        return NGHTTP2_ERR_DEFERRED;

    }

    if (buffs) {
        while (res < length && sess->size) {
            auto const copy = std::min<ssize_t>(buffs->len, length - res);
            memcpy (buf + res, buffs->base, copy);
            res += copy;
            if (copy == buffs->len) {
                sess->size--;
                buffs++;
                continue;
            }
            break;
        }

        if (s->flags & HTTP2_STREAM_SEND_END) {
            if (sess->size)
                s->flags ^= HTTP2_STREAM_SEND_END;
            else
                (*data_flags) |= NGHTTP2_DATA_FLAG_EOF;
        }
    }

    s->transfered_k += res;
    sess->size = res;

    return res;
}

manapi::future<int> ng_wrk_http2_send_response (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) {
    auto const s = conn->as<http_v2_stream_t>();
    if (!s)
        co_return manapi::ERR_ABORTED;

    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    auto &headers = res->headers();
    auto const hs = headers.size() + 1;
    std::unique_ptr<nghttp2_nv, nghttp2_nv_deleter> p (new nghttp2_nv[hs]);
    std::size_t i = 0;
    auto nvs = p.get();
    /* 0 is reserved for :status */
    i = 1;
    for (auto &[h, v] : headers) {
        auto &nv = nvs[i++];
        nv.name = (uint8_t*)h.data();
        nv.namelen = h.size();
        nv.value = reinterpret_cast<uint8_t*>(v.data());
        nv.valuelen = v.size();
        nv.flags = NGHTTP2_NV_FLAG_NO_COPY_NAME|NGHTTP2_NV_FLAG_NO_COPY_VALUE;
    }

    i = 0;
    auto rit = headers.insert({":status", std::to_string(res->status_code())});
    auto it = rit.first;
    auto &nv = nvs[i++];
    nv.name = (uint8_t*)it->first.data();
    nv.namelen = it->first.size();
    nv.value = reinterpret_cast<uint8_t*>(it->second.data());
    nv.valuelen = it->second.size();
    nv.flags = NGHTTP2_NV_FLAG_NO_COPY_NAME|NGHTTP2_NV_FLAG_NO_COPY_VALUE;

    nghttp2_data_provider pr;
    nghttp2_data_provider *fpr;
    if (finish) {
        fpr=nullptr;
    }
    else {
        pr.read_callback = ng_wrk_http2_read_cb;
        fpr = &pr;
    }
    auto const rhs = nghttp2_submit_response(http_v2_ctx->ctx.get(), s->id, nvs, hs, fpr);
    if (rhs) {
        MANAPIHTTP_LOG("nghttp2: failed to send headers due to {}", nghttp2_strerror(rhs));
        co_return manapi::ERR_INTERNAL;
    }

    if (auto const err = nghttp2_session_send(http_v2_ctx->ctx.get())) {
        if (err != NGHTTP2_ERR_WOULDBLOCK)
            co_return manapi::ERR_ABORTED;
    }

    co_return manapi::ERR_OK;
}

manapi::error::status manapi::net::worker::ng_wrk_http2_global_init(manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) {
    try {
        assert(!global->data);

        auto tp = std::make_unique<ng_wrk_http2_ctx_global_t>();

        tp->worker = std::make_shared<worker::http_v2>(w, &ng_wrk_http2_callbacks);
        tp->worker->init(0);

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
    //auto const s = conn->as<http_v2_stream_t>();
    //auto const config = http_v2_ctx->gctx->base_worker->config();

    //return nghttp2_session_want_write(http_v2_ctx->ctx.get());
    return true;
}

int ng_wrk_http2_want_write (const manapi::net::worker::shared_conn &conn) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    if (http_v2_ctx) {
        http_v2_ctx->gctx->base_worker->event_toggle(http_v2_ctx->conn, true, manapi::ev::WRITE);
        for (const auto &stream_conn : http_v2_ctx->streams) {
            auto const s = stream_conn.second->as<http_v2_stream_t>();
            if (s->flags & manapi::ev::WRITE) {
                http_v2_ctx->gctx->worker->feed_event(conn, manapi::ev::WRITE, nullptr, 0, nullptr);
            }
        }
    }
    return 0;
}

manapi::net::worker::connection::ipdata_t * ng_wrk_http2_ipdata (manapi::net::worker::connection *conn) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    return http_v2_ctx->gctx->base_worker->ipdata(http_v2_ctx->conn.get());
}

int ng_wrk_http2_rst (const manapi::net::worker::shared_conn &conn, int code) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    auto const s = conn->as<http_v2_stream_t>();

    if (!(s->flags & manapi::ev::DISCONNECT)) {
        auto const rhs= nghttp2_submit_rst_stream(http_v2_ctx->ctx.get(), 0, s->id, code);
        if (rhs == NGHTTP2_ERR_INVALID_ARGUMENT)
            return manapi::ERR_INVALID_ARGUMENT;
        if (rhs == NGHTTP2_ERR_NOMEM)
            return manapi::ERR_RESOURCE_EXHAUSTED;
    }
    return manapi::ERR_OK;
}

int ng_wrk_http2_on_read_stream (const manapi::net::worker::shared_conn &conn) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    auto const s = conn->as<http_v2_stream_t>();
    if (!s)
        return manapi::ERR_OK;
    if (auto const rhs = manapi::net::worker::http_v2_flush_recv (conn, s))
        return rhs;

    if (!s->recv_size && s->flags & HTTP2_STREAM_IS_READING) {
        s->flags ^= HTTP2_STREAM_IS_READING;
        http_v2_ctx->want_read--;

        if (!http_v2_ctx->want_read)
            http_v2_ctx->gctx->base_worker->event_toggle(http_v2_ctx->conn, true, manapi::ev::READ);
    }

    return manapi::ERR_OK;
}


ssize_t ng_wrk_http2_write (const manapi::net::worker::shared_conn &conn, manapi::ev::buff_t *buff, uint32_t nbuff, bool finish) {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    auto const s = conn->as<http_v2_stream_t>();

    if (s->flags & (HTTP2_STREAM_CLOSED))
        return -1;

    http_v2_ctx->buffs = buff;
    http_v2_ctx->size = static_cast<ssize_t>(nbuff);

    if (finish)
        s->flags |= HTTP2_STREAM_SEND_END;

    if (const auto err = nghttp2_session_resume_data(http_v2_ctx->ctx.get(), s->id)) {
        if (err != NGHTTP2_ERR_INVALID_ARGUMENT) {
            MANAPIHTTP_LOG("nghttp2: resume data send failed due to {}", nghttp2_strerror(err));
            return -1;
        }
    }

    if (const auto err = nghttp2_session_send(http_v2_ctx->ctx.get())) {
        MANAPIHTTP_LOG("nghttp2: send data failed due to {}", nghttp2_strerror(err));
        return -1;
    }

    if (http_v2_ctx->buffs) {
        /* it means that send chunks cb was not executed */
        if (finish && s->flags & HTTP2_STREAM_SEND_END)
            s->flags ^= HTTP2_STREAM_SEND_END;

        http_v2_ctx->buffs = nullptr;
        return 0;
    }

    /* HTTP2_STREAM_SEND_END can be removed by send chunks cb */

    return http_v2_ctx->size;
}

manapi::net::worker::http_v2_callbacks_t ng_wrk_http2_callbacks {
    .http_v2_write = ng_wrk_http2_write,
    .http_v2_on_read_stream = ng_wrk_http2_on_read_stream,
    .http_v2_want_write = ng_wrk_http2_want_write,
    .http_v2_rst_stream = ng_wrk_http2_rst,
    .http_v2_is_writable = ng_wrk_http2_is_writable,
    .http_v2_ip_data = ng_wrk_http2_ipdata
};

#endif