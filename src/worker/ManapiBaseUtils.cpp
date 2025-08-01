#include "worker/ManapiBaseUtils.hpp"

int manapi::net::worker::prepared::event_flags(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    return event_flags(conn, conn->as<connection_prepared_base_t>());
}

int manapi::net::worker::prepared::event_flags(const shared_conn &conn, connection_prepared_base_t *data) MANAPIHTTP_NOEXCEPT {
    return (conn->as<connection_prepared_base_t>()->flags) & base::CONN_MASK_GETTING;
}

manapi::bytebuffer manapi::net::worker::prepared::recv_first_buffer(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<connection_prepared_t>();
    return recv_first_buffer(conn, data);
}

manapi::bytebuffer manapi::net::worker::prepared::recv_first_buffer(const shared_conn &conn, connection_prepared_t *data) MANAPIHTTP_NOEXCEPT {
    auto object = std::move(data->top->recv.deque->buffer);
    data->top->recv.deque = std::move(data->top->recv.deque->next);
    data->top->recv_size--;

    if (!data->top->recv.deque) {
        data->top->recv.last_deque = nullptr;
        assert(object.resize(data->top->recv.deque_cursor).ok());
        data->top->recv.deque_cursor = 0;
    }

    if (data->top->recv.deque_current) {
        object.shift_add(data->top->recv.deque_current);
        data->top->recv.deque_current = 0;
    }

    return std::move(object);
}

void manapi::net::worker::prepared::waiting(const shared_conn &conn, connection_prepared_base_t *data, bool state) MANAPIHTTP_NOEXCEPT {

    if (state)
        data->flags |= base::CONN_IO_WAITING;

    else if (data->flags & base::CONN_IO_WAITING)
        data->flags ^= base::CONN_IO_WAITING;
}

void manapi::net::worker::prepared::waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<connection_prepared_base_t>();
    return waiting(conn, data, state);
}

void manapi::net::worker::prepared::flush_read_(worker::base *w, const shared_conn &conn,connection_prepared_t *data) MANAPIHTTP_NOEXCEPT {
    if (data->top->recv_size) {
        while (data->top->recv.last_deque
            && (data->flags & ev::READ)
            && data->ev_callback) {
            auto object = prepared::recv_first_buffer(conn, data);
            if (!object.empty()) {
                int flags = ev::READ;

                if (data->flags & base::CONN_RECV_END && !data->top->recv_size)
                    flags |= base::CONN_RECV_END;

                if (worker::base::call_user_callback(data->ev_callback.get(), conn, flags, object.data(),
                    static_cast<int>(object.size()), &object))
                    w->close_connection(conn, CLOSE_CONN_ERR);
            }
        }
    }
}

int manapi::net::worker::prepared::flush_read2_(http::config *config, const shared_conn &conn, connection_prepared_t *data) MANAPIHTTP_NOEXCEPT {
    auto s = data->top.get();
    while (s->recv.last_deque && (data->flags & manapi::ev::READ)
        && (data->top->recv_size >= config->max_merge_buffer_stack || (data->flags & base::CONN_RECV_END))) {

        auto b = std::move(s->recv.deque->buffer);
        ssize_t sz;

        s->recv.deque = std::move(s->recv.deque->next);
        s->recv_size--;

        if (!s->recv.deque) {
            s->recv.last_deque = nullptr;
            assert(b.resize(s->recv.deque_cursor).ok());

            s->recv.deque_cursor = 0;
        }

        if (s->recv.deque_current) {
            b.shift_add(s->recv.deque_current);
            s->recv.deque_current = 0;
        }

        sz = static_cast<ssize_t>(b.size());
        if (sz) {
            int flags = manapi::ev::READ;
            if ((data->flags & manapi::net::worker::base::CONN_RECV_END) && !s->recv_size)
                flags |= manapi::net::worker::base::CONN_RECV_END;
            if (worker::base::call_user_callback(data->ev_callback.get(), conn, flags, b.data(), sz, &b))
                return ERR_ABORTED;
        }
    }

    return ERR_OK;
}

bool manapi::net::worker::prepared::is_writable(http::config *config, const shared_conn &conn,connection_prepared_t *data) MANAPIHTTP_NOEXCEPT {
    return data->top->send_size <= config->max_buffer_stack
        && data->transfered < config->speed_limit_rate;
}

std::size_t manapi::net::worker::prepared::recv_count(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const s = conn->as<connection_prepared_t>();
    return recv_count(conn, s);
}

std::size_t manapi::net::worker::prepared::recv_count(const shared_conn &conn, connection_prepared_t *data) MANAPIHTTP_NOEXCEPT {
    return data->top->recv_size;
}

void manapi::net::worker::prepared::top_buffer_clear(connection_prepared_t *s) MANAPIHTTP_NOEXCEPT {
    if (s->top) {
        if (s->top->recv_size) {
            s->top->recv.deque.reset();
            s->top->recv.deque_current = 0;
            s->top->recv.deque_cursor = 0;
            s->top->recv.last_deque = nullptr;
            s->top->recv_size = 0;
        }

        if (s->top->send_size) {
            s->top->send.deque.reset();
            s->top->send.deque_current = 0;
            s->top->send.deque_cursor = 0;
            s->top->send.last_deque = nullptr;
            s->top->send_size = 0;
        }
    }
}

void manapi::net::worker::prepared::event_callback_clear(const shared_conn &conn,connection_prepared_base_t *s) MANAPIHTTP_NOEXCEPT {
    if (s->ev_callback) {
        auto cb = std::move(s->ev_callback);
        if (manapi::net::worker::base::call_user_callback(cb.get(), conn, ev::DISCONNECT, nullptr, 0, nullptr)) {
            /* pass */
        }
    }
}

void manapi::net::worker::prepared::timer_clear(manapi::timer t) MANAPIHTTP_NOEXCEPT {
    if (t) {
        t.stop();
        t.clear();
    }
}

void manapi::net::worker::prepared::feed_event(worker::base *w, const shared_conn &conn, connection_prepared_t *data, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT {
    if (flags & ev::READ) {
        if (flags & base::CONN_TOP_READ) {
            w->feed_event_read_ (conn, data->ev_callback.get(), &data->top->recv, &data->top->recv_size, data->flags, flags, buff, size, p);
            prepared::flush_read_(w, conn, data);
        }
        else {
            prepared::flush_read_(w, conn, data);
            w->feed_event_read_ (conn, data->ev_callback.get(), &data->top->recv, &data->top->recv_size, data->flags, flags, buff, size, p);
        }
    }
    else if (data->ev_callback && (flags & data->flags)) {
        if (manapi::net::worker::base::call_user_callback(data->ev_callback.get(), conn, flags, buff, size, p))
            w->close_connection(conn, CLOSE_CONN_ERR);
    }
}

void manapi::net::worker::prepared::feed_event(worker::base *w, const shared_conn &conn, int flags,const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<connection_prepared_t>();
    return feed_event(w, conn, data, flags, buff, size, p);
}

void manapi::net::worker::prepared::update_limit_rate_connection(const shared_conn &sconn, connection_prepared_base_t *data, worker::base *w, http::config *config, wrk_interface_global_t *global) MANAPIHTTP_NOEXCEPT {
    if (data->transfered >= config->speed_limit_rate
        && data->ev_callback) {
        data->transfered = 0;

        if (data->flags & ev::WRITE && data->ev_callback) {
            if (manapi::net::worker::base::call_user_callback(data->ev_callback.get(), sconn, ev::WRITE, nullptr, 0, nullptr)) {
                w->close_connection(sconn, CLOSE_CONN_ERR);
                return;
            }
        }
        }
    else {
        data->transfered_k += data->transfered;

        if (--data->speed_min_delay <= 0) {
            if (data->flags & (base::CONN_IO_WAITING)
                && (data->transfered_k < config->speed_check_bytes)) {
                w->close_connection(sconn, CLOSE_CONN_EOF);
                return;
            }
            data->transfered_k = 0;
            data->speed_min_delay = static_cast<int>(config->speed_check_delay);
        }
        data->transfered = 0;
    }

    if (sconn->wrk.flags & WRK_INTERFACE_CUSTOM_RATE_LIMIT)
        global->update_limit_rate(sconn, global, w);
}

void manapi::net::worker::prepared::update_limit_rate_connection(const shared_conn &sconn, worker::base *w, http::config *config, wrk_interface_global_t *global) MANAPIHTTP_NOEXCEPT {
    update_limit_rate_connection(sconn, sconn->as<connection_prepared_base_t>(), w, config, global);
}

std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::prepared::event_on(const shared_conn &conn,std::unique_ptr<worker_watcher_cb> callback) MANAPIHTTP_NOEXCEPT {
    auto const conn_data = conn->as<connection_prepared_base_t>();
    auto n = std::exchange(conn_data->ev_callback, std::move(callback));
    return std::move(n);
}









