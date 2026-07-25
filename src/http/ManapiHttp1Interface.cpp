#include <cstring>

#include "ManapiString.hpp"
#include "http/ManapiHttpResponse.hpp"
#include "../include/http/ManapiHttp1Interface.hpp"
#include "../include/http/ManapiHttp1.hpp"
#include "../include/ManapiHttpInternal.hpp"
#include "../include/ManapiUtils.hpp"

#define HTTP_ALL_SWITCH_CASE(namecb__, cb__, ver__, name__) \
case manapi::net::http::versions::ver__: \
httpctx = static_cast<manapi::net::worker::wrk_http_ctx_global_t *> (global->data)->name__.get(); \
return httpctx->cb__;

#define HTTP_ALL_SWITCH_CASE2(namecb__, cb__, ver__, name__) \
case manapi::net::http::versions::ver__: \
httpctx = static_cast<manapi::net::worker::wrk_http_ctx_global_t *> (global->data)->name__.get(); \
if (httpctx->namecb__) { return httpctx->cb__; } break;

#define HTTP_ALL_SWITCH_DEFAULTCASE(name__, cb__) \
default: assert(false && "unreachable code");

#define HTTP_ALL_SWITCH(namecb, ...) \
manapi::net::worker::wrk_interface_global_t * httpctx; \
switch (conn->version) { \
HTTP_ALL_SWITCH_CASE(namecb, namecb(__VA_ARGS__), HTTP_v0_9, http1) \
HTTP_ALL_SWITCH_CASE(namecb, namecb(__VA_ARGS__), HTTP_v1_0, http1) \
HTTP_ALL_SWITCH_CASE(namecb, namecb(__VA_ARGS__), HTTP_v1_1, http1) \
HTTP_ALL_SWITCH_CASE(namecb, namecb(__VA_ARGS__), HTTP_v2, http2) \
HTTP_ALL_SWITCH_CASE(namecb, namecb(__VA_ARGS__), HTTP_v3, http3) \
HTTP_ALL_SWITCH_DEFAULTCASE(namecb, namecb(__VA_ARGS__)) } \
httpctx = static_cast<manapi::net::worker::wrk_http_ctx_global_t *> (global->data)->http1.get(); \
return httpctx->namecb(__VA_ARGS__);

#define HTTP_ALL_SWITCH2(namecb, ...) \
manapi::net::worker::wrk_interface_global_t * httpctx; \
switch (conn->version) { \
HTTP_ALL_SWITCH_CASE2(namecb, namecb(__VA_ARGS__), HTTP_v0_9, http1) \
HTTP_ALL_SWITCH_CASE2(namecb, namecb(__VA_ARGS__), HTTP_v1_0, http1) \
HTTP_ALL_SWITCH_CASE2(namecb, namecb(__VA_ARGS__), HTTP_v1_1, http1) \
HTTP_ALL_SWITCH_CASE2(namecb, namecb(__VA_ARGS__), HTTP_v2, http2) \
HTTP_ALL_SWITCH_CASE2(namecb, namecb(__VA_ARGS__), HTTP_v3, http3) } \
return;

enum http_v1_flags {
    HTTP1_BODY_CHUNKED = 1
};

static int default_wrk_http_all_version (const manapi::net::worker::shared_conn & conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto httpctx = static_cast<manapi::net::worker::wrk_http_ctx_global_t *> (global->data);
    if (httpctx->http1) return manapi::net::http::versions::HTTP_v1_1;
    if (httpctx->http2) return manapi::net::http::versions::HTTP_v2;
    if (httpctx->http3) return manapi::net::http::versions::HTTP_v3;
    return 0;
}

static int default_wrk_http_all_accept (const manapi::net::worker::shared_conn & conn, int flags, const char *buffer, std::size_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    HTTP_ALL_SWITCH (accept_cb, conn, flags, buffer, nsize, p, httpctx, w);
}

static int default_wrk_http_all_init_stream (const manapi::net::worker::shared_conn & conn, const manapi::net::worker::shared_conn & stream, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    if (!conn->version)
        conn->version = default_wrk_http_all_version(conn, global, w);
    HTTP_ALL_SWITCH(init_stream_cb, conn, stream, httpctx, w);
}

static int default_wrk_http_all_init (const manapi::net::worker::shared_conn & conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    if (!conn->version)
        conn->version = default_wrk_http_all_version(conn, global, w);
    HTTP_ALL_SWITCH (init_cb, conn, httpctx, w);
}

static std::size_t default_wrk_http_all_recv_cnt_pending (const manapi::net::worker::shared_conn & conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    HTTP_ALL_SWITCH (recv_cnt_pending, conn, httpctx, w);
}

static manapi::bytebuffer default_wrk_http_all_recv_buf_pending (const manapi::net::worker::shared_conn & conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    HTTP_ALL_SWITCH (recv_buf_pending, conn, httpctx, w);
}

static int default_wrk_http_all_cleanup (manapi::net::worker::connection * conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    if (!conn->version)
        return 0;
    HTTP_ALL_SWITCH (cleanup_cb, conn, httpctx, w);
}

static void default_wrk_http_all_custom_read(const manapi::net::worker::shared_conn & conn, int flags, const char *buffer, std::size_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    HTTP_ALL_SWITCH (custom_read_cb, conn, flags, buffer, nsize, p, httpctx, w);
}

static void default_wrk_http_all_flush_custom_read(const manapi::net::worker::shared_conn & conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    HTTP_ALL_SWITCH (flush_custom_read_cb, conn, httpctx, w);
}

static void default_wrk_http_all_update_limit_rate(const manapi::net::worker::shared_conn & conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    if (!conn->version)
        return;
    HTTP_ALL_SWITCH (update_limit_rate, conn, httpctx, w);
}

static manapi::future<int> default_wrk_http_all_send_response (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) MANAPIHTTP_NOEXCEPT {
    HTTP_ALL_SWITCH(send_response, conn, global, w, res, finish);
}

static int default_wrk_http_all_shutdown_conn(const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, bool force) MANAPIHTTP_NOEXCEPT {
    if (!conn->version)
        return 1;
    HTTP_ALL_SWITCH (shutdown_cb, conn, global, w, force);
}


static int default_wrk_http_all_cleanup_global_cb (manapi::net::worker::wrk_interface_global_t *data, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto wrk_global = static_cast<manapi::net::worker::wrk_http_ctx_global_t *>(data->data);

    int rhs1=0,rhs2=0,rhs3=0;

    if (wrk_global->http1)
        rhs1=wrk_global->http1->cleanup_global_cb(wrk_global->http1.get(), w);

    if (wrk_global->http2)
        rhs2=wrk_global->http2->cleanup_global_cb(wrk_global->http2.get(), w);

    if (wrk_global->http3)
        rhs3=wrk_global->http3->cleanup_global_cb(wrk_global->http3.get(), w);

    delete wrk_global;
    data->data = nullptr;

    return std::max(abs(rhs1),
        std::max(abs(rhs2), abs(rhs3)));
}

static uint64_t default_wrk_http_all_global_flags (const manapi::net::worker::shared_conn & conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto const data = static_cast<manapi::net::worker::wrk_http_ctx_global_t *> (global->data);
    switch (conn->version) {
        case manapi::net::http::versions::HTTP_v0_9:
        case manapi::net::http::versions::HTTP_v1_0:
        case manapi::net::http::versions::HTTP_v1_1:
            return data->http1->flags;
        case manapi::net::http::versions::HTTP_v2:
            return data->http2->flags;
        case manapi::net::http::versions::HTTP_v3:
            return data->http3->flags;
        default:
            return 0;
    }
}

static int default_wrk_http_all_global_alpn (manapi::net::worker::wrk_interface_global_t *global, char const *alpn, std::size_t alpn_size, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    std::string_view s {alpn, alpn_size};
    if (s.starts_with("http/")) {
        s = s.substr(sizeof ("http/") - 1);
        if (s == "0.9")
            return manapi::net::http::versions::HTTP_v0_9;
        if (s == "1.0")
            return manapi::net::http::versions::HTTP_v1_0;
        if (s == "1.1")
            return manapi::net::http::versions::HTTP_v1_1;
    }
    else {
        if (s == "h2")
            return manapi::net::http::versions::HTTP_v2;

        if (s == "h3")
            return manapi::net::http::versions::HTTP_v3;
    }

    return manapi::net::http::versions::HTTP_v1_1;
}

manapi::status manapi::net::worker::default_wrk_http_all_global_init(wrk_interface_global_t *global, worker::interface_worker *w) MANAPIHTTP_NOEXCEPT {
    if (global->data)
        return manapi::status_invalid_argument("global->data already exists");

    global->data = new (std::nothrow) manapi::net::worker::wrk_http_ctx_global_t{};
    if (!global->data)
        return status_resource_exhausted();

    global->alpn_cb = default_wrk_http_all_global_alpn;
    global->flags_cb = default_wrk_http_all_global_flags;
    global->accept_cb = default_wrk_http_all_accept;
    global->init_stream_cb = default_wrk_http_all_init_stream;
    global->init_cb = default_wrk_http_all_init;
    global->recv_cnt_pending = default_wrk_http_all_recv_cnt_pending;
    global->recv_buf_pending = default_wrk_http_all_recv_buf_pending;
    global->cleanup_cb = default_wrk_http_all_cleanup;
    global->custom_read_cb = default_wrk_http_all_custom_read;
    global->flush_custom_read_cb = default_wrk_http_all_flush_custom_read;
    global->update_limit_rate = default_wrk_http_all_update_limit_rate;
    global->cleanup_global_cb = default_wrk_http_all_cleanup_global_cb;
    global->send_response = default_wrk_http_all_send_response;
    global->shutdown_cb = default_wrk_http_all_shutdown_conn;

    return manapi::status_ok();
}

manapi::status manapi::net::worker::default_wrk_http_all_global_add_version(wrk_interface_global_t *global, int version, std::unique_ptr<wrk_interface_global_t> http_t) MANAPIHTTP_NOEXCEPT {
    if (!global)
        return manapi::status_invalid_argument("global is null");


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
            return status_out_of_range("http version incorrect");
    }

    return status_ok();
}

static void default_wrk_http1_custom_read (const manapi::net::worker::shared_conn &conn, int flags, const char *buffer, std::size_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto wrk_ctx = static_cast<manapi::net::worker::wrk_http1_ctx_t *> (conn->wrk.data);

    if (wrk_ctx->flgs & HTTP1_BODY_CHUNKED) {
        auto rhs = manapi::net::http::http_v1_1_chunked_read(wrk_ctx->chunked_ctx.get(), /*&wrk_ctx->req, w,*/ conn,/* w->config(), */buffer, nsize);

        switch (rhs) {
            case manapi::net::http::EHTTP_V1_1_CHUNKED_OK: {
                w->event_flags(conn, manapi::net::worker::base::CONN_RECV_END);
                w->feed_event(conn, manapi::net::worker::base::CONN_RECV_END, nullptr, 0, nullptr);
                break;
            }
            case manapi::net::http::EHTTP_V1_1_CHUNKED_ERR:
                w->close_connection(conn, manapi::net::worker::CLOSE_CONN_ERR);
            break;
            case manapi::net::http::EHTTP_V1_1_CHUNKED_READ: break;
            case manapi::net::http::EHTTP_V1_1_CHUNKED_WAIT: break;
        }
    }
    else {
        assert(false && "not implemented");
    }
}

static int default_wrk_http1_init (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    try {
        assert(!conn->wrk.data);

        auto tp = std::make_unique<manapi::net::worker::wrk_http1_ctx_t>();
        tp->ctx = std::make_unique<manapi::net::http::http_v1_1_t>();
        tp->ctx->config = w->config();
        tp->ctx->req = &tp->req;

        conn->version = manapi::net::http::versions::HTTP_v1_1;

        conn->wrk.data = tp.release();
    }
    catch (std::bad_alloc const &) {
        return manapi::ERR_RESOURCE_EXHAUSTED;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "default_wrk_http1_init", e.what());
        return manapi::ERR_UNKNOWN;
    }

    return manapi::ERR_OK;
}

static int default_wrk_http1_cleanup (manapi::net::worker::connection* conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto wrk_data = static_cast<manapi::net::worker::wrk_http1_ctx_t *>(conn->wrk.data);
    delete wrk_data;
    conn->wrk.data = nullptr;
    conn->wrk.flags = 0;
    return manapi::ERR_OK;
}

static int default_wrk_http1_global_cleanup (manapi::net::worker::wrk_interface_global_t *data, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto wrk_global = static_cast<manapi::net::worker::wrk_http1_ctx_global_t *>(data->data);
    delete wrk_global;
    data->data = nullptr;
    return manapi::ERR_OK;
}

static int default_wrk_http1(const manapi::net::worker::shared_conn &conn, int flags, const char *buffer, std::size_t nsize, manapi::net::worker::ibuffpool_t *p, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto wrk_data = static_cast<manapi::net::worker::wrk_http1_ctx_t *>(conn->wrk.data);
    int status = manapi::net::http::BAD_REQUEST_400;

    if (conn->wrk.flags & manapi::net::worker::WRK_INTERFACE_CONN_RETRY) {
        status = manapi::net::http::SERVICE_UNAVAILABLE_503;
        goto send_error;
    }

    if (flags & manapi::ev::DISCONNECT)
        goto err;

    try {
        if (flags & manapi::ev::READ) {
            auto const config = wrk_data->ctx->config;
            switch (auto const state = manapi::net::http::http_v1_1_work(
                wrk_data->ctx.get(), /*&wrk_data->req, config,*/ &buffer, &nsize)) {
                case manapi::net::http::EHTTP_V1_1_PROTOCOL_PAYLOAD_TOO_LARGE:
                    status = manapi::net::http::PAYLOAD_TOO_LARGE_413;
                    goto send_error;

                case manapi::net::http::EHTTP_V1_1_PROTOCOL_OK:
                    status = manapi::net::http::OK_200;
                    if (!config->contains_http_version(manapi::net::http::versions::http::HTTP_v1_1)) {
                        status = manapi::net::http::UPGRADE_REQUIRED_426;
                        goto send_error;
                    }

                    goto exec;

                case manapi::net::http::EHTTP_V1_1_PROTOCOL_UPGRADE: {
                    w->event_on(conn, nullptr);
                    w->event_flags(conn, 0);

                    auto gctx = static_cast<manapi::net::worker::wrk_http1_ctx_global_t *> (global->data);
                    auto httpall = dynamic_cast<manapi::net::worker::interface_worker *>(w)->wrk_global();
                    auto httpallctx = static_cast<manapi::net::worker::wrk_http_ctx_global_t *> (httpall->data);

                    switch (wrk_data->ctx->http) {
                        case manapi::net::http::versions::HTTP_v0_9: {
                            goto send_error;
                        }
                        case manapi::net::http::versions::HTTP_v1_0: {
                            goto send_error;
                        }
                        case manapi::net::http::versions::HTTP_v1_1: {
                            goto send_error;
                        }
                        case manapi::net::http::versions::HTTP_v2: {
                            if (httpallctx->http2) {
                                global->cleanup_cb(conn.get(), global, w);
                                conn->version = manapi::net::http::versions::HTTP_v2;
                                httpallctx->http2->init_cb(conn, httpallctx->http2.get(), w);
                                w->feed_event(conn, manapi::net::worker::base::CONN_READ|manapi::net::worker::base::CONN_TOP_READ, "PRI * HTTP/2.0", sizeof ("PRI * HTTP/2.0") - 1, nullptr);
                                break;
                            }
                            goto send_error;
                        }
                        case manapi::net::http::versions::HTTP_v3: {
                            /* i got you, bro */
                            goto send_error;
                        }
                        default: {
                            goto send_error;
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
                    goto send_error;
                }
                case manapi::net::http::EHTTP_V1_1_PROTOCOL_WANT_READ: {
                    /* skip */
                    break;
                }
                default: {
                    assert(false && "An invalid state for http_v1_1_work()");
                }
            }
            return manapi::ERR_OK;
exec:
            auto req_ptr = &wrk_data->req;

            auto it_header = req_ptr->headers.find(manapi::net::http::H_EXPECT);
            if (it_header != req_ptr->headers.end()) {
                ssize_t const copy = sizeof ("HTTP/1.1 100 Continue\r\n\r\n") - 1;
                auto const rhs = w->sync_write_ex (conn, static_cast<const char *>("HTTP/1.1 100 Continue\r\n\r\n"),
                    copy, true, WORKER_MAX_CNT);
                if (copy != rhs) {
                    goto err;
                }
            }

            w->waiting(conn, false);

            std::vector<manapi::net::http::header_value_t> trailers_header{};

            it_header = req_ptr->headers.find(manapi::net::http::H_TRANSFER_ENCODING);
            if (it_header != req_ptr->headers.end()) {
                auto rhs = manapi::net::http::parse_header_value(it_header->second);;
                if (!rhs.ok())
                    goto send_error;

                auto const values = rhs.unwrap();
                for (const auto &v : values) {
                    if (manapi::string::equals(v.value, "chunked", 0b10)) {
                        wrk_data->flgs |= HTTP1_BODY_CHUNKED;
                        conn->wrk.flags |= manapi::net::worker::WRK_INTERFACE_CUSTOM_READ;
                        wrk_data->chunked_ctx = std::make_unique<manapi::net::http::http_v1_1_chunked_t>();
                        wrk_data->chunked_ctx->worker = w;
                        wrk_data->chunked_ctx->config = w->config();
                        wrk_data->chunked_ctx->req = req_ptr;
                        wrk_data->chunked_ctx->top_sz = 0;

                        auto trailers_it = wrk_data->req.headers.find("trailer");
                        if (trailers_it != wrk_data->req.headers.end()) {
                            auto trailers_header_status = manapi::net::http::parse_header_value(trailers_it->second);
                            if (!trailers_header_status.ok()) {
                                manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s:%s failed due to %.*s", "http1", "chunked body",
                                    trailers_header_status.message().size(), trailers_header_status.message().data());
                                goto send_error;
                            }

                            trailers_header = trailers_header_status.unwrap();
                        }

                        continue;
                    }

                    goto send_error;
                }
            }

            it_header = req_ptr->headers.find(manapi::net::http::H_CONNECTION);
            if (it_header !=  req_ptr->headers.end()) {
                auto rhs = manapi::net::http::parse_header_value(it_header->second);
                if (!rhs.ok())
                    goto send_error;
                auto const values = rhs.unwrap();

                bool keep_alive_ = false;
                bool close_ = false;

                for (const auto &v : values) {
                    if (manapi::string::equals(v.value, "keep-alive", 0b10))
                        keep_alive_ = true;
                    else if (manapi::string::equals(v.value, "close", 0b10))
                        close_ = true;
                    else
                        goto send_error;
                }

                if (keep_alive_ & close_)
                    goto send_error;

                if (keep_alive_ || !close_)
                    conn->wrk.flags |= manapi::net::worker::WRK_INTERFACE_TCP_KEEP_ALIVE;
            }
            else
                conn->wrk.flags |= manapi::net::worker::WRK_INTERFACE_TCP_KEEP_ALIVE;

            auto cdata = std::make_unique<manapi::net::http::internal::handle_data_t>(conn,
                w->shared_from_this(), req_ptr, std::make_unique<manapi::net::http::internal::cont_callback_cb_t>(
                [w, conn] (bool ok)
                -> void {
                    w->close_connection (conn, ok ? manapi::net::worker::CLOSE_CONN_FINISHED : manapi::net::worker::CLOSE_CONN_ERR);
            }));

            w->event_on(conn, nullptr);
            w->event_flags(conn, 0);


            cdata->router = manapi::net::http::server::cast(w->site().get())->handler(req_ptr);
            cdata->req_data->handler = cdata->router->handler;


            if (wrk_data->req.handler) {
                if (wrk_data->req.handler->trailers.size() > trailers_header.size()) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s:%s failed due to %s", "http1", "chunk body", "trailers too large");
                    goto send_error;
                }

                for (auto &value : trailers_header) {
                    for (auto &c : value.value)
                        c = static_cast<char> (std::tolower(c));

                    if (!wrk_data->req.handler->trailers.contains(value.value)) {
                        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s:%s failed due to %s", "http1", "chunk body", "trailer not allowed");
                        goto send_error;
                    }
                }

                for (auto &value : trailers_header) {
                    auto it = wrk_data->chunked_ctx->trailer_names.insert(std::move(value.value));
                    if (!it.second){
                        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s:%s failed due to %s", "http1", "chunk body", "trailer name duplicate");
                        goto send_error;
                    }
                }
            }

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

            manapi::net::http::internal::handle_income_request(std::move(cdata), status);
        }

        return manapi::ERR_OK;
    }
    catch (std::bad_alloc const &) {

    }
    catch (std::exception const &e) {
        /* fatal error */
        manapi_log_error("%s failed due to %s", "default_wrk_http1", e.what());
    }


    send_error: {
        try {
            auto msg = manapi::net::http::status_to_string(static_cast<uint16_t>(status));
            if (msg.ok()) {
                auto status_message = msg.unwrap();
                auto const s = manapi::net::http::internal::generate_default_page (status, status_message);
                auto const h = std::format("HTTP/{} {} {}\r\ncontent-length: {}\r\nconnection: close\r\n\r\n",
                    manapi::net::http::config::stringify_http_version(conn->version), status, status_message, s.size());
                if (static_cast<ssize_t>(h.size()) != w->sync_write_ex(conn, h.data(), h.size(), false, WORKER_MAX_CNT))
                    goto err;
                if (static_cast<ssize_t>(s.size()) != w->sync_write_ex(conn, s.data(), s.size(), true, WORKER_MAX_CNT))
                    goto err;
                w->close_connection (conn, manapi::net::worker::CLOSE_CONN_SHUTDOWN);
            }
        }
        catch (std::bad_alloc const &) {
            return manapi::ERR_RESOURCE_EXHAUSTED;
        }
        catch (std::exception const &e) {
            manapi_log_error("%s failed due to %s", "default_wrk_http1", e.what());
            return manapi::ERR_INTERNAL;
        }
    }
    err: return manapi::ERR_ABORTED;
}

static void default_wrk_http1_flush_read (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto wrk_ctx = static_cast<manapi::net::worker::wrk_http1_ctx_t *>(conn->wrk.data);
    if (wrk_ctx->flgs & HTTP1_BODY_CHUNKED) {
        switch (manapi::net::http::http_v1_1_chunked_flush(wrk_ctx->chunked_ctx.get(),/* w, */conn)) {
            case manapi::net::http::EHTTP_V1_1_CHUNKED_OK: {
                w->event_flags(conn, manapi::net::worker::base::CONN_RECV_END);
                w->feed_event(conn, manapi::net::worker::base::CONN_RECV_END, nullptr, 0, nullptr);
                break;
            }
            case manapi::net::http::EHTTP_V1_1_CHUNKED_ERR: {
                w->close_connection(conn, manapi::net::worker::CLOSE_CONN_ERR);
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


static std::string stringify_http_info(manapi::net::http::response *res, int version, std::string_view delimiter) {
    return std::format("HTTP/{} {} {}{}", manapi::net::http::config::stringify_http_version(version), std::to_string(res->status_code()), res->status_message(), delimiter);
}

static std::string stringify_headers(manapi::net::http::response *res, std::string_view delimiter) {
    std::string data;

    std::size_t size = 0;
    for (const auto &header : res->headers()) {
        size += header.first.size() + (sizeof (": ") - 1) + header.second.size() + delimiter.size();
    }
    data.reserve(size);

    // add headers
    for (const auto &header: res->headers()) {
        data += header.first;
        data += ": ";
        data += header.second;
        data += delimiter;
    }

    return data;
}

static manapi::bytebuffer default_wrk_http1_recv_buf_pending (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto const wrk_ctx = static_cast<manapi::net::worker::wrk_http1_ctx_t *>(conn->wrk.data);
    if (wrk_ctx->flgs & HTTP1_BODY_CHUNKED) {
        assert(!!wrk_ctx->chunked_ctx);
        auto &top = wrk_ctx->chunked_ctx->top;
        auto object = std::move(top.deque->buffer);
        top.deque = std::move(top.deque->next);
        wrk_ctx->chunked_ctx->top_sz--;

        if (!top.deque) {
            top.last_deque = nullptr;
            auto status = object.resize(top.deque_cursor);
            assert(status.ok());
            top.deque_cursor = 0;
        }

        if (top.deque_current) {
            object.shift_add(top.deque_current);
            top.deque_current = 0;
        }

        return std::move(object);
    } else {
        return w->recv_first_buffer(conn);
    }
}

static std::size_t default_wrk_http1_recv_cnt_pending (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    auto const wrk_ctx = static_cast<manapi::net::worker::wrk_http1_ctx_t *>(conn->wrk.data);
    if (wrk_ctx->flgs & HTTP1_BODY_CHUNKED) {
        assert(!!wrk_ctx->chunked_ctx);
        return wrk_ctx->chunked_ctx->top_sz;
    }
    return w->recv_count(conn);
}

static manapi::future<int> default_wrk_http1_send_response (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global,
    manapi::net::worker::base *w, manapi::net::http::response* res, bool finish) {
    const auto response = stringify_http_info(res, conn->version, "\r\n") + stringify_headers(res, "\r\n") + "\r\n";
    auto rhs = co_await w->fwrite (conn, response.data(), response.size(), finish);
    if (rhs != static_cast<ssize_t>(response.size()))
        co_return manapi::ERR_ABORTED;
    co_return manapi::ERR_OK;
}

manapi::status manapi::net::worker::default_wrk_http1_global_init (manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::interface_worker *w) MANAPIHTTP_NOEXCEPT {
    if (global->data)
        return manapi::status_invalid_argument("global->data already exists");

    global->data = new (std::nothrow) manapi::net::worker::wrk_http1_ctx_global_t{};
    if (!global->data)
        return manapi::status_resource_exhausted();

    global->accept_cb = default_wrk_http1;
    global->cleanup_cb = default_wrk_http1_cleanup;
    global->cleanup_global_cb = default_wrk_http1_global_cleanup;
    global->custom_read_cb = default_wrk_http1_custom_read;
    global->flush_custom_read_cb = default_wrk_http1_flush_read;
    global->init_cb = default_wrk_http1_init;
    global->update_limit_rate = nullptr;
    global->send_response = default_wrk_http1_send_response;
    global->recv_buf_pending = default_wrk_http1_recv_buf_pending;
    global->recv_cnt_pending = default_wrk_http1_recv_cnt_pending;

    return manapi::status_ok();
}

static std::size_t default_wrk_http_recv_cnt_pending (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    return w->recv_count(conn);
}

static manapi::bytebuffer default_wrk_http_recv_buf_pending (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w) MANAPIHTTP_NOEXCEPT {
    return w->recv_first_buffer(conn);
}

void manapi::net::worker::default_wrk_http_preinit(wrk_interface_global_t *global) {
    global->recv_buf_pending = default_wrk_http_recv_buf_pending;
    global->recv_cnt_pending = default_wrk_http_recv_cnt_pending;
}
