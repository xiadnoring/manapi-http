#include "worker/ManapiHttp2Worker.hpp"
#include "http/ManapiHttpResponse.hpp"
#include "../include/ManapiUtils.hpp"

int manapi::net::worker::http_v2_flush_recv(http::config *config, const manapi::net::worker::shared_conn &conn, manapi::net::worker::http_v2_stream_base_t *s) MANAPIHTTP_NOEXCEPT {
    return prepared::flush_read2_(config, conn, s);
}

manapi::net::worker::http_v2::http_v2(worker::base *w, http_v2_callbacks_t *callbacks) : w(w), callbacks(callbacks) {}

manapi::net::worker::http_v2::~http_v2() = default;

const std::shared_ptr<manapi::multithread_storage::worker_t> & manapi::net::worker::http_v2::worker_data() MANAPIHTTP_NOEXCEPT {
    return this->w->worker_data();
}

manapi::net::worker::wrk_interface_global_t * manapi::net::worker::http_v2::wrk_global() MANAPIHTTP_NOEXCEPT {
    return this->w->wrk_global();
}

void manapi::net::worker::http_v2::wrk_global(wrk_interface_global_t *data) MANAPIHTTP_NOEXCEPT {
    this->w->wrk_global(data);
}

manapi::net::http::config * manapi::net::worker::http_v2::config() MANAPIHTTP_NOEXCEPT {
    return this->w->config();
}

manapi::net::http::site & manapi::net::worker::http_v2::site() MANAPIHTTP_NOEXCEPT {
    return this->w->site();
}

void manapi::net::worker::http_v2::waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXCEPT {
    prepared::waiting(conn, state);
}

void manapi::net::worker::http_v2::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT {
    prepared::feed_event(this, conn, flags, buff, size, p);
}

void manapi::net::worker::http_v2::close_connection(shared_conn conn, int flags) MANAPIHTTP_NOEXCEPT {
    if (!conn)
        return;

    auto data = conn->as<http_v2_stream_base_t>();
    if (data->flags & CONN_REMOVED) {
        return;
    }

    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "http2:close_connection() %p flags=%d", data, flags);

    data->flags |= CONN_CLOSED;

    if ((flags & CLOSE_CONN_SHUTDOWN) && data->top->send_size)
         return;

    data->flags |= CONN_REMOVED;

    prepared::event_callback_clear(conn, data);

    conn->cancellation.cancel();

    if ((data->flags & CONN_RECV_END)
        && (data->flags & CONN_SEND_END))
        return;

    /* got something wrong */
    this->callbacks->http_v2_rst_stream(conn, HTTP2_ERROR_NO_ERROR);
}

int manapi::net::worker::http_v2::event_flags(const shared_conn & conn) MANAPIHTTP_NOEXCEPT {
    return prepared::event_flags(conn);
}

int manapi::net::worker::http_v2::event_flags(const shared_conn & conn, int flags) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<http_v2_stream_base_t>();

    MANAPIHTTP_WORKER_EVENT_LOOP_STREAM(data) {
        if (data->ev_callback) {
            if (data->flags & CONN_CLOSED) {
                if (http_v2::call_user_callback(&data->ev_callback, conn, CONN_CLOSED, nullptr, 0, nullptr))
                    this->close_connection(conn, CLOSE_CONN_ERR);
            }
            else {
                if (flags & ev::WRITE) {
                    this->callbacks->http_v2_want_write(conn);
                }

                if (flags & ev::READ) {
                    this->callbacks->http_v2_on_read_stream (conn);

                    if (data->flags & CONN_RECV_END) {
                        if (this->call_user_callback(&data->ev_callback, conn,
                            CONN_RECV_END, nullptr, 0, nullptr))
                            this->close_connection(conn, CLOSE_CONN_ERR);
                    }
                }
            }
        }

        MANAPIHTTP_WORKER_EVENT_BREAK(data)
    }

    return prev;
}

manapi::net::worker::worker_watcher_cb manapi::net::worker::http_v2::event_on(const shared_conn & conn, worker_watcher_cb callback) MANAPIHTTP_NOEXCEPT {
    auto const conn_data = conn->as<http_v2_stream_base_t>();
    return std::exchange(conn_data->ev_callback, std::move(callback));
}

manapi::future<manapi::status> manapi::net::worker::http_v2::init(std::size_t deep) {
    co_return status_ok();
}

manapi::net::worker::connection::ipdata_t * manapi::net::worker::http_v2::ipdata(worker::connection *conn) MANAPIHTTP_NOEXCEPT {
    return this->callbacks->http_v2_ip_data(conn);
}

bool manapi::net::worker::http_v2::is_writable(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const s = conn->as<http_v2_stream_base_t>();
    return this->callbacks->http_v2_is_writable(conn) && prepared::is_writable(this->config(), conn, s);
}

void manapi::net::worker::http_v2::stop(std::function<void()> cb) {
    cb();
}

ssize_t manapi::net::worker::http_v2::sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT {
    return sync_write_ex (conn, buff, nbuff, -1 /* no need */, finish, static_cast<int>(this->w->config()->max_buffer_stack));
}

ssize_t manapi::net::worker::http_v2::sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, std::size_t maxcnt) MANAPIHTTP_NOEXCEPT {
    return this->callbacks->http_v2_write(conn, buff, nbuff, finish);
}

void manapi::net::worker::http_v2::update_limit_rate_stream(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const c = this->config();
    return prepared::update_limit_rate_connection(conn, conn->as<connection_prepared_base_t>(), this, c, c->speed_stream_check_delay, c->speed_stream_check_bytes, this->wrk_global());
}

std::size_t manapi::net::worker::http_v2::recv_count(const shared_conn &conn) const MANAPIHTTP_NOEXCEPT {
    return prepared::recv_count(conn);
}

manapi::bytebuffer manapi::net::worker::http_v2::recv_first_buffer(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    return prepared::recv_first_buffer(conn);
}
