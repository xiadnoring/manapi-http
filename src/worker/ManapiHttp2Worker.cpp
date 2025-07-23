#include "worker/ManapiHttp2Worker.hpp"

#include "ManapiHttpResponse.hpp"
#include "../include/ManapiUtils.hpp"

int manapi::net::worker::http_v2_flush_recv(const manapi::net::worker::shared_conn &conn, manapi::net::worker::http_v2_stream_base_t *s) {
    while (s->recv->last_deque && (s->flags & manapi::ev::READ)) {

        auto b = std::move(s->recv->deque->buffer);
        ssize_t sz;

        s->recv->deque = std::move(s->recv->deque->next);
        s->recv_size--;

        if (!s->recv->deque) {
            s->recv->last_deque = nullptr;
            b.resize(s->recv->deque_cursor);

            s->recv->deque_cursor = 0;
        }

        if (s->recv->deque_current) {
            b.shift_add(s->recv->deque_current);
            s->recv->deque_current = 0;
        }

        sz = static_cast<ssize_t>(b.size());
        if (sz) {
            int flags = manapi::ev::READ;
            if ((s->flags & manapi::net::worker::base::CONN_RECV_END) && !s->recv_size)
                flags |= manapi::net::worker::base::CONN_RECV_END;
            if (worker::base::call_user_callback(s->ev_callback, conn, flags, b.data(), sz, &b))
                return ERR_ABORTED;
        }
    }

    return ERR_OK;
}
manapi::net::worker::http_v2::http_v2(worker::base *w, http_v2_callbacks_t *callbacks) : w(w), callbacks(callbacks) {}

manapi::net::worker::http_v2::~http_v2() = default;

const std::shared_ptr<manapi::multithread_storage::worker_t> & manapi::net::worker::http_v2::worker_data() {
    return this->w->worker_data();
}

manapi::net::worker::wrk_interface_global_t * manapi::net::worker::http_v2::wrk_global() {
    return this->w->wrk_global();
}

void manapi::net::worker::http_v2::wrk_global(wrk_interface_global_t *data) {
    this->w->wrk_global(data);
}

manapi::net::http::config * manapi::net::worker::http_v2::config() {
    return this->w->config();
}

manapi::net::http::site & manapi::net::worker::http_v2::site() {
    return this->w->site();
}

void manapi::net::worker::http_v2::waiting(const shared_conn &conn, bool state) {
    auto const d = conn->as<http_v2_stream_base_t>();
    if (state)
        d->flags |= worker::base::CONN_IO_WAITING;
    else if (d->flags & worker::base::CONN_IO_WAITING)
        d->flags ^= worker::base::CONN_IO_WAITING;
}

void manapi::net::worker::http_v2::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) {
    auto const data = conn->as<http_v2_stream_base_t>();
    if (flags & ev::READ) {
        if (flags & CONN_TOP_READ) {
            this->feed_event_read_ (conn, data->ev_callback.get(), data->recv.get(), &data->recv_size, data->flags, flags, buff, size, p);
            this->callbacks->http_v2_on_read_stream (conn);
        }
        else {
            this->callbacks->http_v2_on_read_stream (conn);
            this->feed_event_read_ (conn, data->ev_callback.get(), data->recv.get(), &data->recv_size, data->flags, flags, buff, size,  p);
        }
    }
    else if ((data->flags & flags) && data->ev_callback) {
        if (this->call_user_callback(data->ev_callback, conn, flags, buff, size, p))
            this->close_connection(conn, CLOSE_CONN_ERR);
    }
}

void manapi::net::worker::http_v2::close_connection(shared_conn conn, int flags) {
    auto data = conn->as<http_v2_stream_base_t>();
    if (data->flags & CONN_REMOVED) {
        return;
    }

    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "http2:close_connection() %p flags=%d", data, flags);

    data->flags |= CONN_CLOSED|CONN_REMOVED;

    if (data->ev_callback) {
        auto cb = std::move(data->ev_callback);
        if (this->call_user_callback(cb, conn, CONN_CLOSED, nullptr, 0, nullptr)) {
            /* skip */
        }
    }

    conn->cancellation.cancel();

    if ((data->flags & CONN_RECV_END)
        && (data->flags & CONN_SEND_END))
        return;

    /* got something wrong */
    this->callbacks->http_v2_rst_stream(conn, HTTP2_ERROR_REFUSED_STREAM);
}

int manapi::net::worker::http_v2::event_flags(const shared_conn & conn) {
    auto const data = conn->as<http_v2_stream_base_t>();
    data->speed_min_delay = static_cast<int>(this->w->config()->speed_check_delay);
    return data->flags & CONN_MASK_GETTING;
}

int manapi::net::worker::http_v2::event_flags(const shared_conn & conn, int flags) {
    auto const data = conn->as<http_v2_stream_base_t>();
    auto const prev = std::exchange(data->flags, ((data->flags >> 2) << 2) | (flags & CONN_MASK_UPDATE));

    if ((data->flags & CONN_CLOSED) && flags && data->ev_callback) {
        if (this->call_user_callback(data->ev_callback, conn, CONN_CLOSED, nullptr, 0, nullptr))
            this->close_connection(conn, CLOSE_CONN_ERR);
        return prev;
    }
    if (flags & ev::WRITE) {
        this->callbacks->http_v2_want_write(conn);
    }
    if (flags & ev::READ
        && data->ev_callback) {
        this->callbacks->http_v2_on_read_stream (conn);
    }
    if ((data->flags & CONN_RECV_END) && (flags & ev::READ) && data->ev_callback) {
        if (this->call_user_callback(data->ev_callback, conn, CONN_RECV_END, nullptr, 0, nullptr))
            this->close_connection(conn, CLOSE_CONN_ERR);
    }
    return prev;
}

std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::http_v2::event_on(const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) {
    auto const conn_data = conn->as<http_v2_stream_base_t>();
    return std::exchange(conn_data->ev_callback, std::move(callback));
}

manapi::error::status manapi::net::worker::http_v2::init(std::size_t deep) {
    return error::status_ok();
}

manapi::net::worker::connection::ipdata_t * manapi::net::worker::http_v2::ipdata(worker::connection *conn) {
    return this->callbacks->http_v2_ip_data(conn);
}

bool manapi::net::worker::http_v2::is_writable(const shared_conn &conn) {
    auto const s = conn->as<http_v2_stream_base_t>();
    return this->callbacks->http_v2_is_writable(conn);
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
    return this->callbacks->http_v2_write(conn, buff, nbuff, finish);
}

void manapi::net::worker::http_v2::update_limit_rate_stream(const shared_conn &conn) {
    auto const conn_data = conn->as<http_v2_stream_base_t>();

    if (--conn_data->speed_min_delay == 0) {
        if (conn_data->flags & CONN_IO_WAITING
            && conn_data->transfered_k < this->w->config()->speed_check_bytes) {
            this->close_connection(conn, CLOSE_CONN_EOF);
            return;
        }
        conn_data->transfered_k = 0;
        conn_data->speed_min_delay = static_cast<int>(this->w->config()->speed_check_delay);
    }
}

std::size_t manapi::net::worker::http_v2::recv_count(const shared_conn &conn) const {
    auto const s = conn->as<http_v2_stream_base_t>();
    return s->recv_size;
}

manapi::bytebuffer manapi::net::worker::http_v2::recv_first_buffer(const shared_conn &conn) {
    auto const s = conn->as<http_v2_stream_base_t>();
    auto b = std::move(s->recv->deque->buffer);

    s->recv->deque = std::move(s->recv->deque->next);
    s->recv_size--;

    if (!s->recv->deque) {
        s->recv->last_deque = nullptr;
        b.resize(s->recv->deque_cursor);

        s->recv->deque_cursor = 0;
    }

    if (s->recv->deque_current) {
        b.shift_add(s->recv->deque_current);
        s->recv->deque_current = 0;
    }

    return std::move(b);
}
