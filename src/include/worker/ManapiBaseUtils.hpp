#pragma once

#include "../ManapiUtils.hpp"
#include "worker/ManapiBaseWorker.hpp"

namespace manapi::net::worker {
    struct connection_base_t {
        ssize_t transfered;
        ssize_t transfered_k;
    };

    struct connection_prepared_base_t : connection_base_t {
        std::unique_ptr<worker_watcher_cb> ev_callback;
        int flags;
        int speed_min_delay;
    };

    struct connection_prepared_t : connection_prepared_base_t {
        std::unique_ptr<struct connection_io> top;
    };

    struct tcp_connection_t : connection_prepared_t {
        manapi::timer t;
        worker::base *worker;
        std::shared_ptr<ev::tcp> watcher;
    };

    struct tls_connection_t : tcp_connection_t {
        void *ssl;
        manapi::timer accept_timer;
        void *rbio;
        void *wbio;
    };

    namespace prepared {
        inline int event_flags (const shared_conn &conn) MANAPIHTTP_NOEXPECT {
            return (conn->as<connection_prepared_base_t>()->flags) & base::CONN_MASK_GETTING;
        }

        inline manapi::bytebuffer recv_first_buffer (const shared_conn &conn) MANAPIHTTP_NOEXPECT {
            auto const data = conn->as<connection_prepared_t>();
            auto object = std::move(data->top->recv.deque->buffer);
            data->top->recv.deque = std::move(data->top->recv.deque->next);
            data->top->recv_size--;

            if (!data->top->recv.deque) {
                data->top->recv.last_deque = nullptr;
                object.resize(data->top->recv.deque_cursor);
                data->top->recv.deque_cursor = 0;
            }

            if (data->top->recv.deque_current) {
                object.shift_add(data->top->recv.deque_current);
                data->top->recv.deque_current = 0;
            }

            return std::move(object);
        }

        inline ssize_t sync_write(interface_worker *w, const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXPECT {
            auto const connection = conn->as<connection_prepared_base_t>();
            auto const config = w->config();
            ssize_t const limit_size = config->speed_limit_rate - connection->transfered;

            auto const size = base::buffs_cut_by_size (buff, nbuff, limit_size, finish);

            if (!size)
                return 0;

            return w->sync_write_ex(conn, buff, nbuff, size, finish, config->max_buffer_stack);
        }

        inline void waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXPECT {
            auto const d = conn->as<connection_prepared_base_t>();

            if (state)
                d->flags |= base::CONN_IO_WAITING;

            else if (d->flags & base::CONN_IO_WAITING)
                d->flags ^= base::CONN_IO_WAITING;
        }

        inline void flush_read_ (interface_worker *w, const shared_conn &conn, connection_prepared_t *data) MANAPIHTTP_NOEXPECT {
            if (data->top->recv_size) {
                while (data->top->recv.last_deque
                    && (data->flags & ev::READ)
                    && data->ev_callback) {
                    auto object = prepared::recv_first_buffer(conn);
                    if (!object.empty()) {
                        if (interface_worker::call_user_callback(data->ev_callback.get(), conn, ev::READ, object.data(),
                            static_cast<int>(object.size()), &object))
                            w->close_connection(conn, CLOSE_CONN_ERR);
                    }
                }
            }
        }

        inline bool is_writable (http::config *config, const shared_conn &conn, connection_prepared_t *data) MANAPIHTTP_NOEXPECT {
            return data->top->send_size <= config->max_buffer_stack
                && data->transfered < config->speed_limit_rate;
        }

        inline std::size_t recv_count (const shared_conn &conn) MANAPIHTTP_NOEXPECT {
            auto const s = conn->as<connection_prepared_t>();
            return s->top->recv_size;
        }

        inline void top_buffer_clear (connection_prepared_t *s) MANAPIHTTP_NOEXPECT {
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

        inline void event_callback_clear (const shared_conn &conn, connection_prepared_base_t *s) MANAPIHTTP_NOEXPECT {
            if (s->ev_callback) {
                auto cb = std::move(s->ev_callback);
                if (manapi::net::worker::base::call_user_callback(cb.get(), conn, ev::DISCONNECT, nullptr, 0, nullptr)) {
                    /* pass */
                }
            }
        }

        inline void timer_clear (manapi::timer t) MANAPIHTTP_NOEXPECT {
            if (t) {
                t.stop();
                t.clear();
            }
        }

        inline void feed_event (interface_worker *w, const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXPECT {
            auto const data = conn->as<connection_prepared_t>();
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
            else if (data->ev_callback) {
                if (manapi::net::worker::base::call_user_callback(data->ev_callback.get(), conn, flags, buff, size, p))
                    w->close_connection(conn, CLOSE_CONN_ERR);
            }
        }

        inline void update_limit_rate_connection (const shared_conn &sconn, interface_worker *w, http::config *config, wrk_interface_global_t *global) MANAPIHTTP_NOEXPECT {
            auto const conn_data = sconn->as<connection_prepared_base_t>();

            if (conn_data->transfered >= config->speed_limit_rate
                && conn_data->ev_callback) {
                conn_data->transfered = 0;

                if (conn_data->flags & ev::WRITE && conn_data->ev_callback) {
                    if (manapi::net::worker::base::call_user_callback(conn_data->ev_callback.get(), sconn, ev::WRITE, nullptr, 0, nullptr)) {
                        w->close_connection(sconn, CLOSE_CONN_ERR);
                        return;
                    }
                }
                }
            else {
                conn_data->transfered_k += conn_data->transfered;

                if (--conn_data->speed_min_delay <= 0) {
                    if (conn_data->flags & (base::CONN_IO_WAITING)
                        && (conn_data->transfered_k < config->speed_check_bytes)) {
                        w->close_connection(sconn, CLOSE_CONN_EOF);
                        return;
                    }
                    conn_data->transfered_k = 0;
                    conn_data->speed_min_delay = static_cast<int>(config->speed_check_delay);
                }
                conn_data->transfered = 0;
            }

            if (sconn->wrk.flags & WRK_INTERFACE_CUSTOM_RATE_LIMIT)
                global->update_limit_rate(sconn, global, w);
        }

        inline std::unique_ptr<manapi::net::worker::worker_watcher_cb> event_on (const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) MANAPIHTTP_NOEXPECT {
            auto const conn_data = conn->as<connection_prepared_base_t>();
            auto n = std::exchange(conn_data->ev_callback, std::move(callback));
            return std::move(n);
        }
    }
}