#include "worker/ManapiHttp3Worker.hpp"

#define MANAPI_AS_STREAM(n__) static_cast<manapi::net::worker::http_v3_stream_base_t *>(n__)

int manapi::net::worker::http_v3_flush_recv(http::config *config, const manapi::net::worker::shared_conn &conn, manapi::net::worker::http_v3_stream_base_t *s) MANAPIHTTP_NOEXPECT {
    return prepared::flush_read2_(config, conn, s);
}

manapi::net::worker::http_v3::http_v3(worker::base *w, http_v3_callbacks_t *callbacks) : w(w), callbacks(callbacks) {}

manapi::net::worker::http_v3::~http_v3() = default;

const std::shared_ptr<manapi::multithread_storage::worker_t> & manapi::net::worker::http_v3::worker_data() MANAPIHTTP_NOEXPECT {
    return this->w->worker_data();
}

manapi::net::worker::wrk_interface_global_t * manapi::net::worker::http_v3::wrk_global() MANAPIHTTP_NOEXPECT {
    return this->w->wrk_global();
}

void manapi::net::worker::http_v3::wrk_global(wrk_interface_global_t *data) MANAPIHTTP_NOEXPECT {
    this->w->wrk_global(data);
}

manapi::net::http::config * manapi::net::worker::http_v3::config() MANAPIHTTP_NOEXPECT {
    return this->w->config();
}

manapi::net::http::site & manapi::net::worker::http_v3::site() MANAPIHTTP_NOEXPECT {
    return this->w->site();
}

void manapi::net::worker::http_v3::waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXPECT {
    return prepared::waiting(conn, MANAPI_AS_STREAM(conn->wrk.data), state);
}

void manapi::net::worker::http_v3::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXPECT {
    return prepared::feed_event(this, conn, MANAPI_AS_STREAM(conn->wrk.data), flags, buff, size, p);
}

void manapi::net::worker::http_v3::close_connection(shared_conn conn, int flags) MANAPIHTTP_NOEXPECT {
    auto data = MANAPI_AS_STREAM (conn->wrk.data);

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
    this->callbacks->http_v3_rst_stream(conn, HTTP3_ERROR_REQUEST_CANCELLED);
}

int manapi::net::worker::http_v3::event_flags(const shared_conn &conn) MANAPIHTTP_NOEXPECT {
    return prepared::event_flags(conn, MANAPI_AS_STREAM(conn->wrk.data));
}

int manapi::net::worker::http_v3::event_flags(const shared_conn &conn, int flags) MANAPIHTTP_NOEXPECT {
    auto const data = conn->as<http_v3_stream_base_t>();

    MANAPIHTTP_WORKER_EVENT_LOOP(data) {
        if (data->ev_callback) {
            if (data->flags & CONN_CLOSED) {
                if (http_v3::call_user_callback(data->ev_callback.get(), conn, CONN_CLOSED, nullptr, 0, nullptr))
                    this->close_connection(conn, CLOSE_CONN_ERR);
            }
            else {
                if (flags & ev::WRITE) {
                    this->callbacks->http_v3_want_write(conn);
                }

                if (flags & ev::READ) {
                    this->callbacks->http_v3_on_read_stream (conn);

                    if (data->flags & CONN_RECV_END) {
                        if (manapi::net::worker::http_v3::call_user_callback(data->ev_callback.get(), conn,
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

std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::http_v3::event_on(const shared_conn &conn, std::unique_ptr<worker_watcher_cb> callback) MANAPIHTTP_NOEXPECT {
    return prepared::event_on(conn, std::move(callback));
}

manapi::future<manapi::error::status> manapi::net::worker::http_v3::init(std::size_t deep) {
    co_return error::status_ok();
}

manapi::net::worker::connection::ipdata_t * manapi::net::worker::http_v3::ipdata(worker::connection *conn) MANAPIHTTP_NOEXPECT {
    return base::ipdata(conn);
}

bool manapi::net::worker::http_v3::is_writable(const shared_conn &conn) MANAPIHTTP_NOEXPECT {
    auto const s = conn->as<http_v3_stream_base_t>();
    return this->callbacks->http_v3_is_writable(conn) && prepared::is_writable(this->config(), conn, s);

}

void manapi::net::worker::http_v3::stop(std::function<void()> cb) {
    cb();
}

ssize_t manapi::net::worker::http_v3::sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXPECT {
    return sync_write_ex (conn, buff, nbuff, -1 /* no need */, finish, static_cast<int>(this->w->config()->max_buffer_stack));
}

ssize_t manapi::net::worker::http_v3::sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) MANAPIHTTP_NOEXPECT {
    return this->callbacks->http_v3_write(conn, buff, nbuff, finish);
}

void manapi::net::worker::http_v3::update_limit_rate_stream(const shared_conn &conn) MANAPIHTTP_NOEXPECT {
    prepared::update_limit_rate_connection(conn, MANAPI_AS_STREAM(conn->wrk.data), this, this->config(), this->wrk_global());
}

std::size_t manapi::net::worker::http_v3::recv_count(const shared_conn &conn) const MANAPIHTTP_NOEXPECT {
    return prepared::recv_count(conn, MANAPI_AS_STREAM(conn->wrk.data));
}

manapi::bytebuffer manapi::net::worker::http_v3::recv_first_buffer(const shared_conn &conn) MANAPIHTTP_NOEXPECT {
    return prepared::recv_first_buffer(conn, MANAPI_AS_STREAM(conn->wrk.data));
}



