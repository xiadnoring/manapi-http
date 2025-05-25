#include "worker/HTTPv2.hpp"
#include "http/HTTPv2.hpp"

#include "ManapiHttpResponse.hpp"

manapi::net::worker::http_v2::http_v2(net::http::site site, bufferpool_t bufferpool, std::shared_ptr<worker::worker_config_t> worker_data, http::config *config) : base(std::move(site), std::move(bufferpool), std::move(worker_data), config) {}

manapi::net::worker::http_v2::~http_v2() = default;

void manapi::net::worker::http_v2::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size) {
    auto const data = conn->as<http::http_v2_stream_t>();
    if (data->ev_callback) {
        data->ev_callback->operator()(conn, flags, buff, size);
    }
}

void manapi::net::worker::http_v2::close_connection(shared_conn conn, bool clean_disconnect) {
    auto data = conn->as<http::http_v2_stream_t>();
    if (data->flags & http::HTTP2_STREAM_REMOVED) {
        return;
    }

    data->flags |= http::HTTP2_STREAM_CLOSED|http::HTTP2_STREAM_REMOVED;

    if (data->ev_callback) {
        data->ev_callback->operator()(conn, http::HTTP2_STREAM_CLOSED, nullptr, 0);
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
    auto headers = std::move(resp->headers());
    co_return co_await http::http_v2_response(this, connection, data, resp->status_code(), std::move(headers), finish);
}

    int manapi::net::worker::http_v2::event_flags(const shared_conn & conn) {
    auto const data = conn->as<http::http_v2_stream_t>();
    data->speed_min_delay = static_cast<int>(this->config_->speed_check_delay);
    return data->flags & (ev::READ|ev::WRITE|ev::DISCONNECT);
}

int manapi::net::worker::http_v2::event_flags(const shared_conn & conn, int flags) {
    auto const data = conn->as<http::http_v2_stream_t>();
    if (flags & ev::WRITE) {
        data->ctx->worker->event_toggle(data->ctx->conn, true, ev::WRITE);
    }
    auto const prev = std::exchange(data->flags, ((data->flags >> 2) << 2) | flags);
    if ((data->flags & http::HTTP2_STREAM_CLOSED) && flags && data->ev_callback) {
        data->ev_callback->operator()(conn, http::HTTP2_STREAM_CLOSED, nullptr, 0);
    }
    if ((data->flags & CONN_RECV_END) && (flags & ev::READ) && data->ev_callback) {
        data->ev_callback->operator()(conn, CONN_RECV_END, nullptr, 0);
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
    return s->write_window && s->ctx->write_window;
}

bool manapi::net::worker::http_v2::is_valid_connection(worker::connection *connection) {
    return true;
}

void manapi::net::worker::http_v2::stop(std::function<void()> cb) {
    cb();
}

ssize_t manapi::net::worker::http_v2::sync_write(const shared_conn &conn, const void *buff, ssize_t size, bool finish) {
    return sync_write_ex (conn, buff, size, finish, static_cast<int>(this->config_->max_buffer_stack));
}

ssize_t manapi::net::worker::http_v2::sync_write_ex(const shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt) {
    auto const data = conn->as<http::http_v2_stream_t>();
    return http::http_v2_write(data, buff, size, finish);
}

void manapi::net::worker::http_v2::update_limit_rate_stream(const shared_conn &conn) {
    auto const conn_data = conn->as<http::http_v2_stream_t>();

    if (--conn_data->speed_min_delay == 0) {
        if (conn_data->transfered_k < this->config_->speed_check_bytes) {
            this->close_connection(conn, false);
            return;
        }
        conn_data->transfered_k = 0;
        conn_data->speed_min_delay = static_cast<int>(this->config_->speed_check_delay);
    }
}