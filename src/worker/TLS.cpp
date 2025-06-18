#include "worker/TLS.hpp"

#include "ManapiParams.hpp"
#include "async/ManapiAsyncSocket.hpp"
#include "ManapiInitTools.hpp"

#include <iostream>
#include <csignal>
#include <utility>
#include <vector>
#include <memory.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <set>


manapi::net::worker::TLS::TLS(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, manapi::net::http::config *config) : TCP(std::move(site), std::move(wdata), config) {}

manapi::net::worker::TLS::~TLS() = default;

void manapi::net::worker::TLS::init() {
    TCP::init();
    auto strtls = this->config_->get_config_param<std::string> (this->config_->ssl, "tls_version", "1.3");
    int tls_version = http::versions::TLS_v1_3;
    if (strtls == "1.3") tls_version = http::versions::TLS_v1_3;
    else if (strtls == "1.2") tls_version = http::versions::TLS_v1_2;
    else if (strtls == "1.1") tls_version = http::versions::TLS_v1_1;
    this->ctx = this->ssl_create_context(tls_version);
    // setup ctx (load certs)
    this->ssl_configure_context();
}

void manapi::net::worker::TLS::configure_connection(const shared_conn & connection, oncont_cb cb) {
    auto conn = connection->as<TLS::connection_interface>();
    cb.call(true);
}

manapi::net::worker::shared_conn manapi::net::worker::TLS::accept(ev::shared_tcp &w) {
    auto connection = TCP::accept(w, [this] () -> shared_conn {
        auto p = std::make_unique<TLS::connection_interface>();
        auto ms = std::shared_ptr<worker::connection> (new worker::connection{p.get()}, connection_interface_eraser);
        p.release();

        auto connection = ms->as<TLS::connection_interface>();

        connection->ssl = this->ssl_new_(this->ctx);

        if (!connection->ssl || !recv_setup_connection (connection)) {
            return nullptr;
        }

        connection->accept_timer = manapi::async::current()->timerpool()->append_timer_sync(8000,
            [this, ms] (manapi::timer t) mutable
            -> void {
                auto conn = std::move(ms);
                this->close_connection(std::move(conn), false);
        });

        return std::move(ms);
    });

    if (!connection)
        return nullptr;

    connection->as<TLS::connection_interface>()->watcher->read_start();

    return std::move(connection);
}

void manapi::net::worker::TLS::close_connection(shared_conn conn, bool clean_disconnect) {
    auto const connection = conn->as<TLS::connection_interface>();

    if (connection->accept_timer) {
        connection->accept_timer.stop();
        connection->accept_timer.clear();
        connection->accept_timer = nullptr;

        clean_disconnect = false;
    }

    TCP::close_connection(conn, clean_disconnect);
}

static long long  zzz = 0;

ssize_t manapi::net::worker::TLS::sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) {
    auto connection = conn->as<TLS::connection_interface>();

    if (connection->status & ev::DISCONNECT)
        return -1;
#if 0
    ssize_t total = 0;

    for (uint32_t i = 0; i < nbuff; ++i) {
        ssize_t res = 0;

        while (res != buff[i].len) {
            if (connection->top->send_size > maxcnt) {
                bool const cfinish = finish && total == size;

                auto const err = this->ssl_bio_flush_write_(conn, connection->wbio,
                    connection->top.get(), maxcnt);

                if (err) {
                    if (err == CONN_IO_WANT_WRITE) {
                        if (this->flush_write_(conn, cfinish))
                            return CONN_IO_ERROR;
                        return total;
                    }

                    return CONN_IO_ERROR;
                }

                if (this->flush_write_(conn, cfinish))
                    return CONN_IO_ERROR;

                if (connection->top->send_size > maxcnt)
                    return total;
            }

            auto rhs = this->ssl_write_(connection->ssl, static_cast<const char *> (buff[i].base) + res,
                static_cast<int>(buff[i].len - res));

            if (rhs >= 0) {
                res += rhs;
                total += rhs;

                connection->transfered += rhs;
            }

            bool const cfinish = finish && total == size;

            int err = this->ssl_get_error_(connection->ssl, rhs);

            if (err) {
                if (err == this->ssl_error_want_read_
                    || err == this->ssl_error_want_write_) {
                    err = this->ssl_bio_flush_write_(conn, connection->wbio,
                        connection->top.get(), maxcnt);

                    if (err) {
                        if (err == CONN_IO_WANT_WRITE) {
                            if (this->flush_write_(conn, cfinish))
                                return CONN_IO_ERROR;
                            return total;
                        }

                        return CONN_IO_ERROR;
                    }

                    if (this->flush_write_(conn, true))
                        return CONN_IO_ERROR;

                    continue;
                }

                return CONN_IO_ERROR;
            }
        }
    }

    bool const cfinish = finish && total == size;

    auto const err = this->ssl_bio_flush_write_(conn, connection->wbio,
                    connection->top.get(), maxcnt);

    if (err) {
        if (err == CONN_IO_WANT_WRITE) {
            if (this->flush_write_(conn, cfinish))
                return CONN_IO_ERROR;
            return total;
        }

        return CONN_IO_ERROR;
    }

    if (this->flush_write_(conn, cfinish))
        return CONN_IO_ERROR;

    return total;
#else
    char buffer[32768];
    size_t cursor = 0;
    size_t lastcur = 0;
    ssize_t total = 0;

    while (nbuff) {
        if (connection->top->send_size > maxcnt)
            break;

        auto const copy = std::min<std::size_t>(buff->len - lastcur, sizeof (buffer) - cursor);
        memcpy (buffer + cursor, buff->base + lastcur, copy);

        lastcur += copy;
        cursor += copy;

        if (lastcur == buff->len) {
            buff++;
            nbuff--;
            lastcur = 0;
        }

        if (cursor == sizeof (buffer) || (!nbuff && cursor)) {
            size_t current = 0;
            while (current != cursor) {
                auto rhs = this->ssl_write_(connection->ssl, buffer + current,
                    static_cast<int>(cursor - current));

                if (rhs > 0)
                    current += rhs;

                int err = this->ssl_get_error_(connection->ssl, rhs);

                if (err) {
                    if (err == this->ssl_error_want_read_
                        || err == this->ssl_error_want_write_) {
                        err = this->ssl_bio_flush_write_(conn, connection, maxcnt);

                        if (err) {
                            if (err == CONN_IO_WANT_WRITE) {
                                if (this->flush_write_(conn, finish && !nbuff))
                                    return CONN_IO_ERROR;
                                return total + current;
                            }

                            return CONN_IO_ERROR;
                        }

                        if (this->flush_write_(conn, true))
                            return CONN_IO_ERROR;

                        continue;
                    }

                    return CONN_IO_ERROR;
                }
            }

            total += current;
            connection->transfered += current;

            cursor = 0;
        }
    }

    bool const cfinish = finish && total == size;

    auto const err = this->ssl_bio_flush_write_(conn, connection, maxcnt);

    if (err) {
        if (err == CONN_IO_WANT_WRITE) {
            if (this->flush_write_(conn, cfinish))
                return CONN_IO_ERROR;
            return total;
        }

        return CONN_IO_ERROR;
    }

    if (this->flush_write_(conn, cfinish))
        return CONN_IO_ERROR;

    return total;
#endif
}

ssize_t manapi::net::worker::TLS::sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) {
    auto const data = conn->as<TLS::connection_interface>();
    ssize_t const limit_size = this->config_->speed_limit_rate - data->transfered;

    auto const size = buffs_cut_by_size (buff, nbuff, limit_size, finish);

    if (!size)
        return size;

    return this->sync_write_ex(conn, buff, nbuff, size, finish, static_cast<int>(this->config_->max_buffer_stack));
}

int manapi::net::worker::TLS::event_flags(const shared_conn & conn, int flags) noexcept(true) {
    auto const data = conn->as<TLS::connection_interface>();
    auto &status = data->status;
    data->speed_min_delay = static_cast<int>(this->config_->speed_check_delay);
    auto const prev = std::exchange(status, ((status >> 2) << 2) | (flags & CONN_MASK_UPDATE));

    if (status & ev::READ) {
        if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
            this->global_.flush_custom_read_cb(conn, &this->global_, this);

        flush_read_ (conn, data);

        if ((status & (CONN_READ|CONN_CLOSED|CONN_REMOVED)) == CONN_READ
            && !data->watcher->is_active()) {
            data->watcher->read_start();
        }
    }

    if ((status & (CONN_READ|CONN_CLOSED|CONN_REMOVED)) == 0
            && data->watcher->is_active()) {
        data->watcher->read_stop();
    }

    if ((status & CONN_RECV_END) && (status & CONN_READ) && data->ev_callback) {
        data->ev_callback->operator()(conn, CONN_RECV_END, nullptr, 0, nullptr);
    }

    return prev;
}

bool manapi::net::worker::TLS::update_limit_rate_connection(const shared_conn &sconn) {
    return TCP::update_limit_rate_connection(sconn);
}

void manapi::net::worker::TLS::connection_interface_eraser(worker::connection *ptr) {
    auto uptr = std::unique_ptr<worker::connection> (ptr);
    auto connection = std::unique_ptr<connection_interface> (uptr->as<connection_interface>());

    if (connection->ssl) {
        auto ssl = std::exchange(connection->ssl, nullptr);
        std::cout << ("SSL FREE\n");
        (dynamic_cast<TLS*>(connection->worker))->ssl_free_(ssl);
    }

    auto const wrk = dynamic_cast<TLS*> (connection->worker);
    if (wrk) {
        wrk->count--;
        wrk->worker_data()->count.fetch_sub(1);

        if (wrk->flags & NET_WORKER_CLOSED
            && !wrk->count
            && wrk->finish) {
            wrk->finish();
            }
    }
}

void manapi::net::worker::TLS::onrecv(std::shared_ptr<ev::tcp> &watcher, const shared_conn &conn, ibuffpool_t buffer) {
    auto buff = buffer.as<char>();
    auto size = static_cast<ssize_t>(buffer.size());
    auto const data = conn->as<TLS::connection_interface>();

    data->transfered += size;
    if (data->transfered >= this->config_->speed_limit_rate) {
        data->watcher->read_stop();
    }

    while (size) {
        auto rhs = this->ssl_bio_write_(data->rbio, buff, static_cast<int>(size));
        if (rhs <= 0) {
            /* error */
            goto err;
        }

        buff += rhs;
        size -= rhs;

        /* TODO: resolve this dump logic */

        while (true) {
            if (ssl_is_init_fininshed_(data->ssl)) {
                if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ) {
                    int cursor = 0;

                    bytebuffer buf{};

                    while (true) {

                        if (!buf)
                            buf = this->bufferpool().buffer(this->config_->buffer_size);

                        auto nread = this->ssl_read_(data->ssl, buf.data() + cursor, static_cast<int>(buf.size()) - cursor);

                        if (nread < 0) {
                            auto const err = this->ssl_get_error_(data->ssl, rhs);
                            if (err == this->ssl_error_ssl_ || err == this->ssl_error_syscall_) {
                                goto err;
                            }
                            nread = 0;
                        }

                        cursor += nread;

                        if (cursor == buf.size() || !nread) {
                            if (cursor)
                                this->global_.custom_read_cb(conn, ev::READ, buf.data(), cursor, &buf, &this->global_, this);

                            cursor = 0;

                            if (!nread) {
                                break;
                            }
                        }
                    }
                }
                else {
                    if (auto const res = this->ssl_bio_flush_read_(conn, data->wbio, &data->top->recv, &data->top->recv_size, 1e5)) {
                        if (res == CONN_IO_ERROR)
                            goto err;
                    }
                }
            }
            else {
                rhs = this->ssl_accept_(data->ssl);
                auto const status = this->ssl_get_error_(data->ssl, rhs);

                if (status == this->ssl_error_want_read_ || status == this->ssl_error_want_write_) {
                    /* force write all data */
                    if (this->ssl_bio_flush_write_(conn, data, 1e5)) {
                        goto err;
                    }

                    if (this->flush_write_(conn, true))
                        goto err;
                }
                else if (status) {
                    goto err;
                }

                if (ssl_is_init_fininshed_ (data->ssl)) {
                    data->accept_timer.stop();
                    data->accept_timer.clear();
                    data->accept_timer = nullptr;
                    continue;
                }
            }

            break;
        }
    }

    if (auto const res = this->ssl_flush_recv(conn, &data->top->recv, &data->top->recv_size)) {
        goto err;
    }

    if(this->check_read_stack_full_(data)) {
        goto err;
    }

    return;

    err: {
        auto const cdata = conn->as<TLS::connection_interface>();

        cdata->status |= ev::DISCONNECT;
        conn->cancellation.cancel();

        return;
    }
}

// void manapi::net::worker::TLS::onaccept_event_(const shared_conn &conn) {
//     this->event_on(conn.get(),
//         std::make_unique<worker_watcher_cb>([this]
//         (const shared_conn &conn, int flags, ibuffpool_t buffer) mutable
//         -> void {
//             this->accept_work_ (conn, flags, std::move(buffer));
//     }));
//     conn->as<TLS::connection_interface>()->watcher->read_start();
// }


int manapi::net::worker::TLS::check_read_stack_full_(connection_interface *data) {
    if (data->top->recv_size >= this->config_->max_buffer_stack) {
        /* sadness */
        if (auto rhs = data->watcher->read_stop()) {
            return rhs;
        }
    }
    return 0;
}

int manapi::net::worker::TLS::ssl_bio_flush_write_(const shared_conn &conn, TLS::connection_interface *m, int max_cnt) {
    int rhs;
    int flags = 0;
    buffer_deque *parent = nullptr;
    auto top = &m->top->send;

    char fastfast[65536];
    ssize_t nfastfast = 0;

    try {
        while (true) {
            if (max_cnt < m->top->send_size)
                break;

            if (max_cnt < 10000)
                nfastfast = std::min<ssize_t>((max_cnt - m->top->send_size + 1) * this->config_->buffer_size,
                    (sizeof (fastfast)));
            else
                nfastfast = sizeof (fastfast);

            rhs = this->ssl_bio_read_(m->wbio, fastfast,
                static_cast<int>(nfastfast));

            if (rhs > 0) {
                ssize_t alr = 0;
                if (!m->top->send_size) {
                    alr = m->watcher->try_write(fastfast, rhs);
                    if (alr < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            /* fatal error */
                            return CONN_IO_ERROR;

                        alr = 0;
                    }

                    if (alr == rhs)
                        continue;
                }

                auto const prev = m->top->send_size;
                TLS::connection_io_send(top, fastfast + alr, rhs - alr, &this->bufferpool(),
                    this->config_->buffer_size, &m->top->send_size, 1e5);
                m->top->cur_send_size += m->top->send_size - prev;

                this->flush_write_(conn, false);

            }
            else {
                if (!this->ssl_bio_should_retry_(m->wbio)) {
                    return CONN_IO_ERROR;
                }
                break;
            }
        }
        return CONN_IO_OK;
    }
    catch (std::exception const &e) {
        manapi::async::current()->logger()->error(manapi::logger::default_service,
            manapi::ERR_INTERNAL, "TLS::ssl_bio_flush_write_(...): {}", e.what());
    }

    // try {
    //     do {
    //         if (!top->last_deque || top->last_deque->buffer.size() == top->deque_cursor) {
    //             if (this->flush_write_(conn, false))
    //                 return CONN_IO_ERROR;
    //
    //             if (m->send_size > max_cnt)
    //                 return CONN_IO_WANT_WRITE;
    //
    //             auto buffer = this->bufferpool().buffer(this->config_->buffer_size);
    //
    //             auto obj = std::make_unique<buffer_deque>(std::move(buffer), nullptr);
    //             if (top->last_deque) {
    //                 parent = top->last_deque;
    //                 top->last_deque->next = std::move(obj);
    //                 top->last_deque = top->last_deque->next.get();
    //             }
    //             else {
    //                 parent = nullptr;
    //                 top->deque = std::move(obj);
    //                 top->last_deque = top->deque.get();
    //                 top->deque_current = 0;
    //             }
    //
    //             top->deque_cursor = 0;
    //             flags |= 1 /* an empty buffer was created */;
    //             m->send_size++;
    //             m->cur_send_size++;
    //         }
    //         else if (flags)
    //             flags = 0;
    //
    //         rhs = this->ssl_bio_read_(wbio, top->last_deque->buffer.data() + top->deque_cursor,
    //             static_cast<int>(top->last_deque->buffer.size() - top->deque_cursor));
    //
    //         if (rhs > 0) {
    //             top->deque_cursor += rhs;
    //         }
    //         else {
    //             if (!this->ssl_bio_should_retry_(wbio)) {
    //                 return CONN_IO_ERROR;
    //             }
    //             rhs = 0;
    //         }
    //
    //         if (!rhs && (flags /* an empty buffer was created */ )) {
    //             /* remove an empty buffer at the end */
    //             auto const prev = m->send_size;
    //             connection_io_trim(top, parent, &m->send_size);
    //             m->cur_send_size -= prev - m->send_size;
    //         }
    //     }
    //     while (rhs > 0);
    //
    //     return CONN_IO_OK;
    // }
    // catch (std::exception const &e) {
    //     manapi::async::current()->logger()->error(manapi::logger::default_service,
    //         manapi::ERR_INTERNAL, "TLS::ssl_bio_flush_write_(...): {}", e.what());
    // }

    return CONN_IO_ERROR;
}

int manapi::net::worker::TLS::ssl_bio_flush_read_(const shared_conn &conn, void *rbio, connection_io_part *top, int *cnt, int max_cnt) {
    int rhs;
    int err;
    int flags = 0;
    buffer_deque *parent = nullptr;
    auto data = conn->as<TLS::connection_interface>();

    do {
        if (top->last_deque) {
            assert (top->last_deque->buffer.size() >= top->deque_cursor);
        }
        if (!top->last_deque || top->last_deque->buffer.size() == top->deque_cursor) {
            if (auto const res = ssl_flush_recv(conn, top, cnt)) {
                return res;
            }

            if (this->check_read_stack_full_(conn->as<TLS::connection_interface>())) {
                return CONN_IO_ERROR;
            }

            if (cnt && *cnt >= max_cnt)
                return CONN_IO_WANT_READ;

            auto buffer = this->bufferpool().buffer(1, this->config_->buffer_size);

            auto obj = std::make_unique<buffer_deque>(std::move(buffer), nullptr);
            if (top->last_deque) {
                parent = top->last_deque;
                top->last_deque->next = std::move(obj);
                top->last_deque = top->last_deque->next.get();
            }
            else {
                parent = nullptr;
                top->deque = std::move(obj);
                top->last_deque = top->deque.get();
                top->deque_current = 0;
            }

            top->deque_cursor = 0;
            flags = 1 /* an empty buffer was created */;
            if (cnt)
                (*cnt)++;
        }
        else
            flags = 0;

        rhs = this->ssl_read_(data->ssl, top->last_deque->buffer.data() + top->deque_cursor,
            static_cast<int>(top->last_deque->buffer.size() - top->deque_cursor));

        if (rhs >= 0) {
            top->deque_cursor += rhs;
            if (!rhs && (flags /* an empty buffer was created */ )) {
                /* remove an empty buffer at the end */
                //connection_io_trim(top, parent, cnt);
            }
        }
        else {
            err = this->ssl_get_error_(data->ssl, rhs);
            if (err == this->ssl_error_ssl_ || err == this->ssl_error_syscall_) {
                return CONN_IO_ERROR;
            }
        }
    }
    while (rhs > 0);

    if (auto const res = ssl_flush_recv(conn, top, cnt)) {
        return res;
    }

    return CONN_IO_OK;
}

int manapi::net::worker::TLS::ssl_flush_recv(const shared_conn &conn, connection_io_part *top, int *cnt) {
    auto data = conn->as<TLS::connection_interface>();

    try {
        while (top->last_deque
            && data->status & ev::READ
            && data->ev_callback) {
            auto object = std::move(top->deque->buffer);
            top->deque = std::move(top->deque->next);

            if (!top->deque) {
                object.resize(top->deque_cursor);
                top->last_deque = nullptr;
                top->deque_cursor = 0;
            }

            if (top->deque_current) {
                object.shift_add(top->deque_current);
                top->deque_current = 0;
            }

            if (cnt)
                (*cnt)--;

            if (!object.empty()) {
                data->ev_callback->operator()(conn, ev::READ, object.data(),
                    static_cast<int>(object.size()), &object);
            }
        }

        if (data->status & ((CONN_READ|CONN_CLOSED|CONN_REMOVED)) == CONN_READ
            && !data->watcher->is_active()) {
            data->watcher->read_start();
        }

        return CONN_IO_OK;
    }
    catch (std::exception const &e) {
        std::cerr << e.what() << "\n";
    }

    return CONN_IO_ERROR;
}