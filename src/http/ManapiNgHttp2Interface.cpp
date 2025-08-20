#include "ManapiUtils.hpp"
#include "../include/ManapiUtils.hpp"

#if MANAPIHTTP_NGHTTP2_DEPENDENCY

#include <cstring>

#include "nghttp2/nghttp2.h"
#include "nghttp2/nghttp2ver.h"

#include "http/ManapiHttpResponse.hpp"
#include "http/ManapiURLDecodeStream.hpp"
#include "http/ManapiBaseHttp.hpp"
#include "../include/ManapiSiteInternal.hpp"
#include "../include/http/ManapiNgHttp2Interface.hpp"

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
    HTTP2_STREAM_IS_READING = manapi::net::worker::base::CONN_MAX_CODE << 1,
    HTTP2_STREAM_TRAILERS = manapi::net::worker::base::CONN_MAX_CODE << 2
    //HTTP2_STREAM_START_WORK_WAIT = 2048
};

enum http2_stream_ctx_flags {
    HTTP2_CTX_WANT_CLOSE = 1
};

struct manapi::net::worker::ng_wrk_http2_ctx_global_t {
    std::shared_ptr<net::worker::http_v2> http2;
    net::worker::base *worker;
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

static int ng_wrk_http2_cleanup (manapi::net::worker::connection *conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto wrk_data = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(conn->wrk.data);
    delete wrk_data;
    conn->wrk.flags = 0;
    conn->wrk.data = nullptr;
    return manapi::ERR_OK;
}

static int ng_wrk_http2_write (nghttp2_session *s) MANAPIHTTP_NOEXCEPT {
    if (int const rhs = nghttp2_session_send (s)) {
        if (rhs != NGHTTP2_ERR_WOULDBLOCK) {
            manapi_log_trace("%s failed due to %s", "nghttp2_session_send", nghttp2_strerror(rhs));
            return manapi::ERR_ABORTED;
        }
    }
    return manapi::ERR_OK;
}

static void ng_wrk_http2_on_close (const manapi::net::worker::shared_conn &conn) {
    auto wrk_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    if (!wrk_ctx)
        return;

    if (wrk_ctx->streams.empty()
        && wrk_ctx->flgs & HTTP2_CTX_WANT_CLOSE) {
        wrk_ctx->ctx.reset();
        wrk_ctx->conn.reset();
    }
}

static void ng_wrk_http2_rst_streams (manapi::net::worker::ng_wrk_http2_ctx_t *ctx) {
    for (const auto &s : ctx->streams) {
        ctx->gctx->http2->close_connection(s.second, manapi::net::worker::CLOSE_CONN_ERR);
    }
}

static int ng_wrk_http2_on_stream_close_callback (nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *user_data) {
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    auto conn = static_cast<manapi::net::worker::shared_conn *>(nghttp2_session_get_stream_user_data(session, stream_id));
    if (!conn)
        return 0;

    if (nghttp2_session_set_stream_user_data(sess->ctx.get(), stream_id, nullptr))
        return manapi::ERR_INVALID_ARGUMENT;

    // delete stream
    return 0;
}

static int ng_wrk_http2(const manapi::net::worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
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
            wrk_ctx->gctx->worker->event_toggle(wrk_ctx->conn, false, manapi::ev::READ);
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

static ssize_t ng_wrk_http2_send_callback(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data) {
    auto const s = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);

    auto const rhs = s->gctx->worker->sync_write(s->conn, data,
        static_cast<ssize_t>(length), true);

    if (rhs > 0)
        return rhs;

    if (rhs == 0)
        return NGHTTP2_ERR_WOULDBLOCK;

    return NGHTTP2_ERR_CALLBACK_FAILURE;
}

static void close_connection (manapi::net::worker::shared_conn conn, manapi::net::worker::shared_conn sconn, manapi::net::worker::base *w, bool ok) MANAPIHTTP_NOEXCEPT {
    auto const sdata = sconn->as<http_v2_stream_t>();
    auto ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(conn->wrk.data);

    auto const flags = ok ? manapi::net::worker::CLOSE_CONN_SHUTDOWN : manapi::net::worker::CLOSE_CONN_ERR;
    ctx->gctx->http2->close_connection(sconn, flags);
    auto it = ctx->streams.find(sdata->id);
    assert(it != ctx->streams.end());
    if (it != ctx->streams.end()) {
        ng_wrk_http2_on_stream_close_callback(ctx->ctx.get(), sdata->id, 0,
            ctx);
        ctx->streams.erase(it);
    }

    //manapi::net::http::http_v2_on_close_stream(ctx->ctx.get(), sdata->id);

    if (ctx->streams.empty()) {
        w->waiting(conn, true);
        ng_wrk_http2_on_close (conn);
    }
}

static int ng_wrk_http2_on_frame_recv_callback (nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    switch (frame->hd.type) {
        case NGHTTP2_DATA: {

            auto const conn = static_cast<manapi::net::worker::shared_conn *>(nghttp2_session_get_stream_user_data(session, frame->hd.stream_id));
            if (!conn)
                return 0;

            auto s = (*conn)->as<http_v2_stream_t>();
            if (!s)
                return 0;

            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                s->flags |= HTTP2_STREAM_RECV_END;
                sess->gctx->http2->feed_event(*conn, HTTP2_STREAM_RECV_END, nullptr, 0, nullptr);
            }

            break;
        }
        case NGHTTP2_HEADERS: {
            auto const conn = static_cast<manapi::net::worker::shared_conn *>(nghttp2_session_get_stream_user_data(session, frame->hd.stream_id));
            if (!conn)
                return 0;

            try {
                auto s = (*conn)->as<http_v2_stream_t>();

                if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                    s->flags |= HTTP2_STREAM_RECV_END;
                    sess->gctx->http2->feed_event(*conn, HTTP2_STREAM_RECV_END, nullptr, 0, nullptr);
                }

                if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
                    if (s->flags & HTTP2_STREAM_TRAILERS) {

                    }
                    else {
                        s->flags |= HTTP2_STREAM_TRAILERS;

                        sess->gctx->worker->waiting(sess->conn, false);

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

                        auto hit = s->req->headers.find(manapi::net::http::header::CONTENT_LENGTH);
                        if (hit == s->req->headers.end()) {
                            s->req->body_size = -1;
                        }
                        else {
                            try {
                                s->req->body_size = std::stoll(hit->second);
                            }
                            catch (std::exception const &e) {
                                manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s: %s",
                                    "nghttp2", e.what());
                                s->req->body_size = -1;
                            }

                            if (s->req->body_size < 0) {
                                status = manapi::net::http::BAD_REQUEST_400;
                                s->req->body_size = 0;
                            }
                        }

                        manapi::async::current()->etaskpool()->append_task(
                            [status, conn = sess->conn, id = frame->hd.stream_id, w = sess->gctx->worker, httpw = sess->gctx->http2] () -> void {
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
                                try {
                                    auto cdata = std::make_unique<manapi::net::http::internal::handle_data_t>(s->second, httpw,
                                        req_ptr, std::make_unique<manapi::net::http::internal::cont_callback_cb_t>(
                                        [w, sconn = s->second, conn] (bool ok) mutable
                                        -> void {
                                            MANAPIHTTP_MUST_ALLOC_START
                                            manapi::async::current()->etaskpool()->append_task(
                                                [w, ok, conn, sconn] () mutable  -> void {
                                                    close_connection(std::move(conn), std::move(sconn), w, ok);
                                            });
                                            MANAPIHTTP_MUST_ALLOC_END
                                    }));

                                    // this->event_on(conn, std::unique_ptr<worker_watcher_cb>(nullptr));
                                    // this->event_flags(conn, 0);

                                    cdata->router = w->site().handler(req_ptr);
                                    manapi::net::http::internal::handle_income_request(std::move(cdata), status);
                                }
                                catch (std::exception const &e) {
                                    manapi_log_error("%s: %s failed due to %s", "nghttp2", "client start", e.what());

                                    MANAPIHTTP_MUST_ALLOC_START
                                    manapi::async::current()->etaskpool()->append_task(
                                        [w, conn, sconn = s->second] () mutable  -> void {
                                            close_connection(std::move(conn), std::move(sconn), w, false);
                                    });
                                    MANAPIHTTP_MUST_ALLOC_END
                                }
                        });
                    }
                }
            }
            catch (std::bad_alloc const &e) {
                return NGHTTP2_ERR_NOMEM;
            }
            catch (std::exception const &e) {
                manapi_log_error(e.what());
                return NGHTTP2_ERR_FATAL;
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
#if (NGHTTP2_VERSION_NUM >= 0x013000)
        case NGHTTP2_PRIORITY_UPDATE: {
            break;
        }
#endif
        default:
            break;
    }
    return 0;
}

static int ng_wrk_http2_on_header_callback (nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags, void *user_data) {
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    switch (frame->hd.type) {
        case NGHTTP2_HEADERS: {

            auto conn = static_cast<manapi::net::worker::shared_conn *>(nghttp2_session_get_stream_user_data(session, frame->hd.stream_id));
            if (!conn)
                break;

            auto s = (*conn)->as<http_v2_stream_t>();
            auto const config = sess->gctx->worker->config();

            std::string_view name_str (reinterpret_cast<const char *>(name), namelen);
            std::string_view value_str (reinterpret_cast<const char *>(value), valuelen);

            if (config->max_header_key_size < name_str.size())
                return -1;

            if (config->max_header_value_size < value_str.size())
                return -1;

            if (s->flags & HTTP2_STREAM_TRAILERS) {
                s->req->trailers_size += name_str.size() + value_str.size();

                if (!s->req->handler || s->req->trailers_size > s->req->handler->trailers_size) {
                    return NGHTTP2_ERR_TOO_MANY_CONTINUATIONS;
                }

                auto it = s->req->trailers.find(name_str);
                if (it == s->req->trailers.end()) {
                    s->req->trailers.insert({std::string(name_str), std::string(value_str)});
                }
                else {
                    it->second.append(", ");
                    it->second.append(value_str);
                }
            }
            else {
                s->req->headers_size += name_str.size() + value_str.size();

                if (s->req->headers_size > config->max_headers_size) {
                    return NGHTTP2_ERR_TOO_MANY_CONTINUATIONS;
                }

                try {
                    auto it = s->req->headers.find(name_str);
                    if (it == s->req->headers.end()) {
                        s->req->headers.insert({std::string(name_str), std::string(value_str)});
                    }
                    else {
                        it->second.append(", ");
                        it->second.append(value_str);
                    }
                }
                catch (std::bad_alloc const &) {
                    return NGHTTP2_ERR_NOMEM;
                }
                catch (std::exception const &e) {
                    manapi_log_error(e.what());
                    return NGHTTP2_ERR_FATAL;
                }
            }

            break;
        }
        default:
            break;
    }
    return 0;
}

static int ng_wrk_http2_data_chunk_recv_callback (nghttp2_session *session, uint8_t flags, int32_t stream_id, const uint8_t *data, size_t len, void *user_data) {
    auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
    auto conn = static_cast<manapi::net::worker::shared_conn *>(nghttp2_session_get_stream_user_data(session, stream_id));
    if (!conn)
        return NGHTTP2_ERR_STREAM_CLOSED;

    auto const s = (*conn)->as<http_v2_stream_t>();

    auto const config = sess->gctx->worker->config();

    auto rhs = manapi::net::worker::base::connection_io_send(&s->top->recv, reinterpret_cast<const char*>(data), len, &sess->gctx->worker->bufferpool(),
        config->buffer_size, &s->top->recv_size, 1e5);

    if (rhs != len)
        return NGHTTP2_ERR_NOMEM;

    s->transfered_k += rhs;

    if (manapi::net::worker::http_v2_flush_recv(config, *conn, s))
        return NGHTTP2_ERR_CALLBACK_FAILURE;

    if (manapi::net::worker::prepared::read_buffs_is_full(s->top.get(), config)
            && !(s->flags & HTTP2_STREAM_IS_READING)) {
        s->flags |= HTTP2_STREAM_IS_READING;
        sess->want_read ++;
    }

    return 0;
}

static void ng_wrk_http2_stream_deleter (manapi::net::worker::connection *w) {
    if (!w)
        return;

    std::unique_ptr<manapi::net::worker::connection> s (w);
    auto const data = w->as<http_v2_stream_t>();

    delete data;
}

static int ng_wrk_http2_on_begin_headers_callback(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    try {
        auto const sess = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *>(user_data);
        if (frame->hd.type != NGHTTP2_HEADERS ||
                    frame->headers.cat != NGHTTP2_HCAT_REQUEST)
            return 0;
        // create connection

        auto const id = frame->hd.stream_id;

        auto tp = std::make_unique<http_v2_stream_t>();
        tp->speed_min_delay = static_cast<decltype(tp->speed_min_delay)>(sess->gctx->worker->config()->speed_check_delay);
        tp->id = id;
        tp->top = std::make_unique<manapi::net::worker::connection_io>();
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
    catch (std::bad_alloc const &) {
        return NGHTTP2_ERR_NOMEM;
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
    }
    return NGHTTP2_ERR_FATAL;
}

static int ng_wrk_http2_init (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
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

        auto const config = wrk_ctx->gctx->worker->config();

        nghttp2_settings_entry settings[] = {
            {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, static_cast<uint32_t>(config->max_concurrent_streams >= 0 ? config->max_concurrent_streams : 100)},
            {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, static_cast<uint32_t>(config->max_hpack_list_size < 0 ? 4096 : config->max_hpack_list_size)},
            {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, static_cast<uint32_t>(config->initial_window_size < 0 ? 65535 : config->initial_window_size)},
            {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, static_cast<uint32_t>(config->max_frame_size < 0 ? 16384 : config->max_frame_size)}
        };

        if (auto rhs = nghttp2_submit_settings(s, NGHTTP2_FLAG_NONE, settings, 4)) {
            return manapi::ERR_UNKNOWN;
        }

        w->event_on(conn,
            [w, global]
            (const manapi::net::worker::shared_conn & conn, int flags, const char *buffer, ssize_t nsize, manapi::net::worker::ibuffpool_t *p)
            -> void {
            ng_wrk_http2(conn, flags, buffer, nsize, p, global, w);
        });

        w->event_flags(conn, manapi::ev::READ|manapi::ev::WRITE);

        return manapi::ERR_OK;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "ng_wrk_http2_init", e.what());
        return manapi::ERR_UNKNOWN;
    }
}

static void ng_wrk_http2_update_limit_rate (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    try {
        auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
        if (http_v2_ctx) {
            for (auto const &s : http_v2_ctx->streams) {
                static_cast<manapi::net::worker::ng_wrk_http2_ctx_global_t *> (global->data)
                    ->http2->update_limit_rate_stream(s.second);
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "ng_wrk_http2_update_limit_rate", e.what());
    }
}

static int ng_wrk_http2_global_cleanup (manapi::net::worker::wrk_interface_global_t *data, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
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

static ssize_t ng_wrk_http2_read_cb (nghttp2_session *session, int32_t stream_id, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void *user_data) {
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
        manapi::async::current()->etaskpool()->append_task([http2 = sess->gctx->http2, conn = *conn] () -> void {
            http2->feed_event(conn, manapi::ev::WRITE, nullptr, 0, nullptr);
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

static int ng_wrk_http2_send_response_sync (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) MANAPIHTTP_NOEXCEPT {
    auto const s = conn->as<http_v2_stream_t>();
    if (!s)
        return manapi::ERR_ABORTED;

    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);

    auto &headers = res->headers();
    auto const hs = headers.size() + 1;
    nghttp2_nv p[hs];

    try {
        std::size_t i = 0;
        /* 0 is reserved for :status */
        i = 1;
        for (auto &[h, v] : headers) {
            auto &nv = p[i++];
            nv.name = (uint8_t*)h.data();
            nv.namelen = h.size();
            nv.value = reinterpret_cast<uint8_t*>(v.data());
            nv.valuelen = v.size();
            nv.flags = NGHTTP2_NV_FLAG_NO_COPY_NAME|NGHTTP2_NV_FLAG_NO_COPY_VALUE;
        }

        i = 0;
        auto rit = headers.insert({":status", std::to_string(res->status_code())});
        auto it = rit.first;
        auto &nv = p[i++];
        nv.name = (uint8_t*)it->first.data();
        nv.namelen = it->first.size();
        nv.value = reinterpret_cast<uint8_t*>(it->second.data());
        nv.valuelen = it->second.size();
        nv.flags = NGHTTP2_NV_FLAG_NO_COPY_NAME|NGHTTP2_NV_FLAG_NO_COPY_VALUE;
    }
    catch (std::bad_alloc const &) {
        return manapi::ERR_RESOURCE_EXHAUSTED;
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return manapi::ERR_INTERNAL;
    }

    nghttp2_data_provider pr;
    nghttp2_data_provider *fpr;
    if (finish) {
        fpr=nullptr;
    }
    else {
        pr.read_callback = ng_wrk_http2_read_cb;
        fpr = &pr;
    }
    auto const rhs = nghttp2_submit_response(http_v2_ctx->ctx.get(), s->id, p, hs, fpr);
    if (rhs) {
        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s: %s failed due to %s",
            "nghttp2", "send headers", nghttp2_strerror(rhs));
        return manapi::ERR_INTERNAL;
    }

    if (auto const err = nghttp2_session_send(http_v2_ctx->ctx.get())) {
        if (err != NGHTTP2_ERR_WOULDBLOCK)
            return manapi::ERR_ABORTED;
    }

    return manapi::ERR_OK;
}

static manapi::future<int> ng_wrk_http2_send_response (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) {
    co_return ng_wrk_http2_send_response_sync(conn, global, w, res, finish);
}

manapi::error::status manapi::net::worker::ng_wrk_http2_global_init(manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::interface_worker *w) MANAPIHTTP_NOEXCEPT {
    try {
        assert(!global->data);

        auto tp = std::make_unique<ng_wrk_http2_ctx_global_t>();

        tp->http2 = std::make_shared<worker::http_v2>(w, &ng_wrk_http2_callbacks);
        // tp->worker->init(0);

        tp->worker = w;

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
        return manapi::error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "nghttp2: init", e.what());
        return manapi::error::status_unknown("nghttp2: init");
    }
}

static bool ng_wrk_http2_is_writable (const manapi::net::worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    //auto const s = conn->as<http_v2_stream_t>();
    //auto const config = http_v2_ctx->gctx->worker->config();

    //return nghttp2_session_want_write(http_v2_ctx->ctx.get());
    return true;
}

static int ng_wrk_http2_want_write (const manapi::net::worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    if (http_v2_ctx) {
        http_v2_ctx->gctx->worker->event_toggle(http_v2_ctx->conn, true, manapi::ev::WRITE);
        for (const auto &stream_conn : http_v2_ctx->streams) {
            auto const s = stream_conn.second->as<http_v2_stream_t>();
            if (s->flags & manapi::ev::WRITE) {
                http_v2_ctx->gctx->http2->feed_event(conn, manapi::ev::WRITE, nullptr, 0, nullptr);
            }
        }
    }
    return 0;
}

static manapi::net::worker::connection::ipdata_t * ng_wrk_http2_ipdata (manapi::net::worker::connection *conn) MANAPIHTTP_NOEXCEPT {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    return http_v2_ctx->gctx->worker->ipdata(http_v2_ctx->conn.get());
}

static int ng_wrk_http2_rst (const manapi::net::worker::shared_conn &conn, int code) MANAPIHTTP_NOEXCEPT {
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

static int ng_wrk_http2_on_read_stream (const manapi::net::worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const http_v2_ctx = static_cast<manapi::net::worker::ng_wrk_http2_ctx_t *> (conn->wrk.data);
    auto const s = conn->as<http_v2_stream_t>();
    if (!s)
        return manapi::ERR_OK;
    auto const config = http_v2_ctx->gctx->worker->config();
    if (auto const rhs = manapi::net::worker::http_v2_flush_recv (config, conn, s))
        return rhs;

    if (!s->top->recv_size && s->flags & HTTP2_STREAM_IS_READING) {
        s->flags ^= HTTP2_STREAM_IS_READING;
        http_v2_ctx->want_read--;

        if (!http_v2_ctx->want_read)
            http_v2_ctx->gctx->worker->event_toggle(http_v2_ctx->conn, true, manapi::ev::READ);
    }

    return manapi::ERR_OK;
}


static ssize_t ng_wrk_http2_write (const manapi::net::worker::shared_conn &conn, manapi::ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT {
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
            manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s: %s failed due to %s",
                "nghttp2", "nghttp2_session_resume_data", nghttp2_strerror(err));
            goto err;
        }
    }

    if (const auto err = nghttp2_session_send(http_v2_ctx->ctx.get())) {
        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "%s: %s failed due to %s",
            "nghttp2", "nghttp2_session_send", nghttp2_strerror(err));
        goto err;
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

    err: {
        http_v2_ctx->buffs = nullptr;
        http_v2_ctx->gctx->http2->close_connection(conn, manapi::net::worker::CLOSE_CONN_ERR);
        return -manapi::ERR_INTERNAL;
    }
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