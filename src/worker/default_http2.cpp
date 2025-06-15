#include "worker/default_http2.hpp"
#include "http/HTTPv2.hpp"

#include "ManapiHttpResponse.hpp"

manapi::net::worker::http_v2::http_v2(worker::base *w) : w(w) {}

manapi::net::worker::http_v2::~http_v2() = default;

const std::shared_ptr<manapi::net::worker::worker_config_t> & manapi::net::worker::http_v2::worker_data() {
    return this->w->worker_data();
}

manapi::net::http::config * manapi::net::worker::http_v2::config() {
    return this->w->config();
}

manapi::net::http::site & manapi::net::worker::http_v2::site() {
    return this->w->site();
}

void manapi::net::worker::http_v2::waiting(const shared_conn &conn, bool state) {
    auto const d = conn->as<http::http_v2_stream_t>();
    if (state)
        d->flags |= http::HTTP2_STREAM_IO_WAITING;
    else if (d->flags & http::HTTP2_STREAM_IO_WAITING)
        d->flags ^= http::HTTP2_STREAM_IO_WAITING;
}

void manapi::net::worker::http_v2::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) {
    auto const data = conn->as<http::http_v2_stream_t>();
    if (flags & ev::READ) {
        if (flags & CONN_TOP_READ) {
            this->feed_event_read_ (conn, data->ev_callback.get(), data->recv.get(), &data->recv_size, data->flags, flags, buff, size, p);
            http::http_v2_on_read_stream (conn, data);
        }
        else {
            http::http_v2_on_read_stream (conn, data);
            this->feed_event_read_ (conn, data->ev_callback.get(), data->recv.get(), &data->recv_size, data->flags, flags, buff, size,  p);
        }
    }
    else if (data->ev_callback) {
        data->ev_callback->operator()(conn, flags, buff, size, p);
    }
}

void manapi::net::worker::http_v2::close_connection(shared_conn conn, bool clean_disconnect) {
    auto data = conn->as<http::http_v2_stream_t>();
    if (data->flags & http::HTTP2_STREAM_REMOVED) {
        return;
    }

    data->flags |= http::HTTP2_STREAM_CLOSED|http::HTTP2_STREAM_REMOVED;

    if (data->ev_callback) {
        data->ev_callback->operator()(conn, http::HTTP2_STREAM_CLOSED, nullptr, 0, nullptr);
    }

    conn->cancellation.cancel();

    if (!clean_disconnect) {
        if (http::http_v2_rst_stream(data, manapi::net::http::HTTP2_ERROR_CONNECT_ERROR)) {
            /* failed :( */
        }
    }
}

void manapi::net::worker::http_v2::configure_connection(const shared_conn &conn, oncont_cb cb) {
    cb.call(true);
}

manapi::future<ssize_t> manapi::net::worker::http_v2::response(const shared_conn &connection, http::response *resp, bool finish) {
    auto const data = connection->as<http::http_v2_stream_t>();
    co_return co_await http::http_v2_response(this, connection,
        data, resp->status_code(), std::move(resp->headers()), finish);
}

int manapi::net::worker::http_v2::event_flags(const shared_conn & conn) {
    auto const data = conn->as<http::http_v2_stream_t>();
    data->speed_min_delay = static_cast<int>(this->w->config()->speed_check_delay);
    return data->flags & CONN_MASK_GETTING;
}

int manapi::net::worker::http_v2::event_flags(const shared_conn & conn, int flags) {
    auto const data = conn->as<http::http_v2_stream_t>();
    auto const prev = std::exchange(data->flags, ((data->flags >> 2) << 2) | (flags & CONN_MASK_UPDATE));

    if ((data->flags & http::HTTP2_STREAM_CLOSED) && flags && data->ev_callback) {
        data->ev_callback->operator()(conn, http::HTTP2_STREAM_CLOSED, nullptr, 0, nullptr);
        return prev;
    }
    if (flags & ev::WRITE) {
        data->ctx->worker->event_toggle(data->ctx->conn, true, ev::WRITE);
    }
    if (flags & ev::READ
        && data->ev_callback) {
        http::http_v2_on_read_stream (conn, data);
    }
    if ((data->flags & CONN_RECV_END) && (flags & ev::READ) && data->ev_callback) {
        data->ev_callback->operator()(conn, CONN_RECV_END, nullptr, 0, nullptr);
    }
    return prev;
}

std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::http_v2::event_on(const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) {
    auto const conn_data = conn->as<http::http_v2_stream_t>();
    return std::exchange(conn_data->ev_callback, std::move(callback));
}

void manapi::net::worker::http_v2::init() {

}

manapi::net::worker::connection::ipdata_t * manapi::net::worker::http_v2::ipdata(worker::connection *conn) {
    auto const data = conn->as<http::http_v2_stream_t>();
    return data->ctx->conn->ipdata.get();
}

bool manapi::net::worker::http_v2::is_writable(const shared_conn &conn) {
    auto const s = conn->as<http::http_v2_stream_t>();
    return (s->write_window && s->ctx->write_window)
        && !(s->flags & http::HTTP2_STREAM_PRIORITY_LOCKED)
        && !(s->ctx->flags & http::HTTP2_CTX_FLAG_BLOCK_WRITE);
}

bool manapi::net::worker::http_v2::is_valid_connection(worker::connection *connection) {
    return true;
}

void manapi::net::worker::http_v2::stop(std::function<void()> cb) {
    cb();
}

ssize_t manapi::net::worker::http_v2::sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) {
    return sync_write_ex (conn, buff, nbuff, -1 /* no need */, finish, static_cast<int>(this->w->config()->max_buffer_stack));
}

ssize_t manapi::net::worker::http_v2::sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) {
    auto const data = conn->as<http::http_v2_stream_t>();
    return http::http_v2_write(conn, data, buff, nbuff, finish);
}

void manapi::net::worker::http_v2::update_limit_rate_stream(const shared_conn &conn) {
    auto const conn_data = conn->as<http::http_v2_stream_t>();

    if (--conn_data->speed_min_delay == 0) {
        if (conn_data->flags & http::HTTP2_STREAM_IO_WAITING
            && conn_data->transfered_k < this->w->config()->speed_check_bytes) {
            this->close_connection(conn, false);
            return;
        }
        conn_data->transfered_k = 0;
        conn_data->speed_min_delay = static_cast<int>(this->w->config()->speed_check_delay);
    }
}