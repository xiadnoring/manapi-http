#include "worker/ManapiTlsOverTcp.hpp"

#include "ManapiParams.hpp"
#include "async/ManapiAsyncSocket.hpp"
#include "ManapiInitTools.hpp"
#include "../include/ManapiUtils.hpp"
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

struct ssl_bio_deleter_t;

enum conn_tls_flags  {
    CONN_TLS_SHUTDOWN = 512,
    CONN_TLS_EARLY_DATA = 1024,
    CONN_TLS_EARLY_FINISHED = 2048
};

manapi::net::worker::TLS::TLS(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config) : TCP(std::move(site), std::move(wdata), config) {}

manapi::net::worker::TLS::~TLS() = default;

void manapi::net::worker::TLS::init(std::size_t deep) {
    TCP::init(deep + 1);
}

manapi::net::worker::shared_conn manapi::net::worker::TLS::accept(const ev::shared_tcp &w) {
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
                this->close_connection(std::move(conn), CLOSE_CONN_ERR);
        });

        return std::move(ms);
    });

    if (!connection)
        return nullptr;

    auto data = connection->as<TLS::connection_interface>();
    if (!(data->status & CONN_CLOSED)) {
        this->read_start_(data);
    }

    return std::move(connection);
}

void manapi::net::worker::TLS::close_connection(shared_conn conn, int flags) {
    auto const connection = conn->as<TLS::connection_interface>();

    if (connection->status & CONN_REMOVED)
        return;

    manapi_log_trace(debug::LOG_TRACE_LOW, "TLS:close_connection() %p flags=%d", connection, flags);

    if ((connection->status & CONN_TLS_SHUTDOWN)) {
        if ((flags & (CLOSE_CONN_EOF)))
            goto eof;

        return;
    }

    connection->status |= CONN_TLS_SHUTDOWN;

    if (!this->config_->force_conn_shutdown
        && (
            (flags & (CLOSE_CONN_SHUTDOWN))
            || (!flags
                && (!this->config_->keep_alive
                    || !(conn->wrk.flags & WRK_INTERFACE_TCP_KEEP_ALIVE)
                    )
                ))) {

        if (conn->wrk.flags & WRK_INTERFACE_TCP_KEEP_ALIVE)
            conn->wrk.flags ^= WRK_INTERFACE_TCP_KEEP_ALIVE;

        if (connection->ev_callback) {
            auto cb = std::move(connection->ev_callback);
            if (this->call_user_callback(cb, conn, ev::DISCONNECT, nullptr, 0, nullptr)) {
                /* skip */
            }
        }

        this->waiting(conn, true);

        this->shutdown_async_(conn);

        return;
    }

    eof:
    if (flags & CLOSE_CONN_EOF)
        this->ssl_set_shutdown_(connection->ssl,
            this->ssl_recv_shutdown_|this->ssl_send_shutdown_);

    if (connection->accept_timer) {
        connection->accept_timer.stop();
        connection->accept_timer.clear();
        connection->accept_timer = nullptr;

        flags = CLOSE_CONN_EOF;
    }

    TCP::close_connection(conn, flags);

    if ((CONN_TLS_SHUTDOWN & connection->status) && !(connection->status & CONN_REMOVED))
        connection->status ^= CONN_TLS_SHUTDOWN;
}

ssize_t manapi::net::worker::TLS::sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) {
    auto connection = conn->as<TLS::connection_interface>();

    if (connection->status & (ev::DISCONNECT))
        return -1;

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
                int rhs;

                if (connection->status & CONN_TLS_EARLY_DATA) {
                    std::size_t writebytes;
                    auto res = this->ssl_write_early_data_(connection->ssl, buffer + current,
                        cursor - current, &writebytes);
                    if (!res)
                        rhs = 0;
                    else
                        rhs = static_cast<int>(writebytes);
                }
                else
                    rhs = this->ssl_write_(connection->ssl, buffer + current,
                        static_cast<int>(cursor - current));

                if (rhs > 0)
                    current += rhs;
                else {
                    int err = this->ssl_get_error_(connection->ssl, rhs);

                    if (err) {
                        if (err == this->ssl_error_want_read_
                            || err == this->ssl_error_want_write_) {
                            auto const erhs = this->ssl_bio_flush_write_(conn, connection, maxcnt);

                            if (erhs) {
                                if (erhs == CONN_IO_WANT_WRITE) {
                                    if (this->flush_write_(conn, true))
                                        return CONN_IO_ERROR;
                                    return total + current;
                                }

                                return CONN_IO_ERROR;
                            }

                            if (this->flush_write_(conn, true))
                                return CONN_IO_ERROR;

                            if (err == this->ssl_error_want_read_)
                                return total + current;

                            continue;
                            }

                        return CONN_IO_ERROR;
                    }

                    if (err == this->ssl_error_zero_return_) {
                        this->close_connection(conn, CONN_TLS_SHUTDOWN);
                        return CONN_IO_ERROR;
                    }

                    if (err == this->ssl_error_ssl_)
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
            if (this->flush_write_(conn, true))
                return CONN_IO_ERROR;
            return total;
        }

        return CONN_IO_ERROR;
    }

    if (this->flush_write_(conn, cfinish))
        return CONN_IO_ERROR;

    return total;
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

        if ((status & (CONN_READ|CONN_CLOSED|CONN_REMOVED)) == CONN_READ) {
            this->read_start_(data);
        }
    }

    if ((status & (CONN_READ|CONN_CLOSED|CONN_REMOVED)) == 0) {
        this->read_stop_(data);
    }

    if ((status & CONN_RECV_END) && (status & CONN_READ) && data->ev_callback) {
        if(this->call_user_callback(data->ev_callback,conn, CONN_RECV_END, nullptr, 0, nullptr))
            this->close_connection(conn, CLOSE_CONN_ERR);
    }

    return prev;
}

void manapi::net::worker::TLS::update_limit_rate_connection(const shared_conn &sconn) {
    return TCP::update_limit_rate_connection(sconn);
}

void manapi::net::worker::TLS::connection_interface_eraser(worker::connection *ptr) {
    auto uptr = std::unique_ptr<worker::connection> (ptr);
    auto connection = std::unique_ptr<connection_interface> (uptr->as<connection_interface>());
    auto w = (dynamic_cast<TLS*>(connection->worker));

    if (connection->watcher) {
        if (connection->watcher->is_active()) {
            manapi_log_trace(debug::LOG_TRACE_LOW, "TCP:%p read_stop()", connection.get());
            connection->watcher->read_stop();
        }
        manapi::async::current()->eventloop()->stop_watcher(std::move(connection->watcher));
    }

    if (connection->ssl) {
        auto ssl = std::exchange(connection->ssl, nullptr);
        manapi_log_trace(debug::LOG_TRACE_MEDIUM, "TLS:Free SSL %p conn", connection.get());
        if (w)
            w->ssl_free_(ssl);
    }

    auto const wrk = dynamic_cast<TLS*> (connection->worker);
    if (wrk) {
        if (wrk->global_.cleanup_cb(ptr, &wrk->global_, wrk))
            MANAPIHTTP_LOG2("tcp this->global_.cleanup_cb failed");

        wrk->count--;
        wrk->worker_data()->as<http::server_ctx::worker_data_t>()->count.fetch_sub(1);

        if (wrk->flags & NET_WORKER_CLOSED
            && !wrk->count
            && wrk->finish)
            wrk->finish();
    }
}

void manapi::net::worker::TLS::shutdown_async_(shared_conn conn) {
    auto s = conn->as<TLS::connection_interface>();

    this->read_start_(s);

    if (!ssl_is_init_fininshed_(s->ssl)) {
        manapi_do_handshake_(conn, s);
        return;
    }

    if (s->accept_timer) {
        s->accept_timer.stop();
        s->accept_timer = nullptr;
    }

    bool flg = false;
    while (true) {
        auto rhs = this->ssl_shutdown_(s->ssl);
        manapi_log_trace(debug::LOG_TRACE_LOW, "TLS:Shutdown %p = %d", s, rhs);

        if (!rhs) {
            goto write;
        }

        if (rhs==1) {
            // if (rhs!=1)
            //     this->ssl_set_shutdown_(s->ssl, this->ssl_recv_shutdown_|this->ssl_send_shutdown_);
            TCP::close_connection(conn, CLOSE_CONN_EOF);
            return;
        }

        // if (rhs) {
        //     this->ssl_set_shutdown_(s->ssl, this->ssl_recv_shutdown_|this->ssl_send_shutdown_);
        //     TCP::close_connection(conn, CLOSE_CONN_EOF);
        //     return;
        // }

        auto const err = this->ssl_get_error_(s->ssl, rhs);

        if (err == this->ssl_error_syscall_) {
            auto const prev = std::exchange(flg, true);
            if (prev)
                goto err;
            continue;
        }

        if (err == this->ssl_error_want_read_) {
            goto write;
        }

        if (err == this->ssl_error_want_write_) {
            if (this->ssl_bio_flush_write_(conn, s, 1e5))
                goto err;
            if (this->flush_write_(conn, true))
                goto err;
            if (!s->top->send_size) {
                flg = !flg;
                if (flg)
                    continue;
                return;
            }
            this->event_on(conn, std::make_unique<worker_watcher_cb>(
                [this, conn2 = conn] (const shared_conn & conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p)
                    -> void {
                    if (flags & ev::DISCONNECT) {
                        goto err;
                    }
                    try {
                        if (flags & ev::WRITE) {
                            this->shutdown_async_(conn);
                        }
                        return;
                    }
                    catch (std::exception const &e) {
                        std::cerr << e.what() << "\n";
                    }
                    err:
                    this->event_on(conn, nullptr);
                    TCP::close_connection(conn, CLOSE_CONN_ERR);
            }));
            this->event_flags(conn, ev::WRITE);
            return;
        }
        goto err;
    }

    write:
    if (this->ssl_bio_flush_write_(conn, s, 1e5))
        goto err;
    if (this->flush_write_(conn, true))
        goto err;
    return;
    err:
    s->status ^= CONN_TLS_SHUTDOWN;
    TCP::close_connection(conn, CLOSE_CONN_ERR);
}

void manapi::net::worker::TLS::onrecv(const std::shared_ptr<ev::tcp> &watcher, const shared_conn &conn, ibuffpool_t buffer) {
    auto buff = buffer.as<char>();
    auto size = static_cast<ssize_t>(buffer.size());
    auto const data = conn->as<TLS::connection_interface>();

    data->transfered += size;
    if (data->transfered >= this->config_->speed_limit_rate) {
        this->read_stop_(data);
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

        while (!(data->status & CONN_CLOSED)) {
            if (ssl_is_init_fininshed_(data->ssl)) {
                rhs = manapi_do_process (conn, data);
                if (rhs == CONN_IO_ERROR)
                    goto err;
            }
            else {
                if (data->status & CONN_TLS_EARLY_DATA
                    && !(data->status & CONN_TLS_EARLY_FINISHED)) {
                    {

                        std::size_t readbytes;
                        auto const early_res = this->ssl_read_early_data_ (data->ssl,
                            nullptr, 0, &readbytes);

                        if (early_res == this->early_data_read_finish_) {
                            data->status ^= CONN_TLS_EARLY_DATA;
                            data->status |= CONN_TLS_EARLY_FINISHED;
                            manapi_log_trace(debug::LOG_TRACE_LOW,
                                "TLS:Early data was read %p", data);
                            continue;
                        }

                        if (early_res == this->early_data_read_error_) {
                            auto err = this->ssl_get_error_(data->ssl, early_res);
                            if (err == this->ssl_error_ssl_)
                                goto err;

                            if (err == this->ssl_error_want_read_ ||
                                err == this->ssl_error_want_write_) {
                                if (this->ssl_bio_flush_write_(conn, data, 1e5))
                                    goto err;
                                if (this->flush_write_(conn, true))
                                    goto err;

                                break;
                            }
                        }
                    }

                    rhs = manapi_do_process (conn, data);
                    if (rhs == CONN_IO_ERROR)
                        goto err;
                }
                else {
                    rhs = manapi_do_handshake_ (conn, data);
                    switch (rhs) {
                        case CONN_IO_OK:
                            continue;
                        case CONN_IO_AGAIN:
                            break;
                        case CONN_IO_ERROR:
                            default:
                                goto err;
                    }
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

        this->close_connection(conn, CLOSE_CONN_EOF);

        return;
    }
}

int manapi::net::worker::TLS::check_read_stack_full_(connection_interface *data) {
    if (data->top->recv_size >= this->config_->max_buffer_stack) {
        /* sadness */
        this->read_stop_(data);
    }
    return 0;
}

int manapi::net::worker::TLS::manapi_do_process(const shared_conn &conn, connection_interface *data) {
    if (data->status & CONN_TLS_SHUTDOWN) {
        this->shutdown_async_(conn);
    }
    else {
        if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ) {
            int cursor = 0;

            bytebuffer buf{};

            while (true) {

                if (!buf)
                    buf = this->bufferpool().buffer(this->config_->buffer_size);

                int nread;
                if (data->status & CONN_TLS_EARLY_DATA) {
                    std::size_t readbytes;
                    auto early_res = this->ssl_read_early_data_(data->ssl, buf.data() + cursor,
                        buf.size() - cursor, &readbytes);
                    if (early_res == this->early_data_read_error_) {
                        nread = 0;
                    }
                    else {
                        nread = static_cast<int>(readbytes);

                        if (early_res == this->early_data_read_finish_) {
                            data->status |= CONN_TLS_EARLY_FINISHED;
                            auto const res = this->manapi_do_handshake_(conn, data);
                            switch (res) {
                                case CONN_IO_OK:
                                    data->status ^= CONN_TLS_EARLY_DATA;
                                    manapi_log_trace(debug::LOG_TRACE_LOW,
                                        "TLS:Early data was read %p", data);
                                    break;
                                case CONN_IO_ERROR:
                                    return CONN_IO_ERROR;
                                case CONN_IO_AGAIN:
                                    break;
                                default:
                                    return CONN_IO_ERROR;
                            }
                        }
                    }
                }
                else
                    nread = this->ssl_read_(data->ssl, buf.data() + cursor, static_cast<int>(buf.size()) - cursor);

                if (nread <= 0) {
                    auto const err = this->ssl_get_error_(data->ssl, nread);
                    if (err == this->ssl_error_want_read_ || err == this->ssl_error_want_write_) {
                        /* force write all data */
                        if (this->ssl_bio_flush_write_(conn, data, 1e5)) {
                            return CONN_IO_ERROR;
                        }

                        if (this->flush_write_(conn, true))
                            return CONN_IO_ERROR;
                    }
                    else if (err == this->ssl_error_zero_return_) {

                    }
                    else
                        return CONN_IO_ERROR;

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
            if (auto const res = this->ssl_bio_flush_read_(conn, data, 1e5)) {
                if (res == CONN_IO_ERROR)
                    return CONN_IO_ERROR;
            }
        }
    }

    return CONN_IO_OK;
}

int manapi::net::worker::TLS::manapi_do_handshake_(const shared_conn &conn, connection_interface *data) {
    if (!(data->status & (CONN_TLS_EARLY_DATA|CONN_TLS_EARLY_FINISHED)) && this->ssl_early_data_is_enabled_(this->ctx)) {
        data->status |= CONN_TLS_EARLY_DATA;
        manapi_log_trace(debug::LOG_TRACE_LOW, "TLS:Try early data %p", data);
        return CONN_IO_OK;
    }

    int rhs = this->ssl_accept_(data->ssl);
    manapi_log_trace(debug::LOG_TRACE_LOW, "TLS:Handshake() %p = %d", data, rhs);

    if (rhs <= 0) {
        auto const status = this->ssl_get_error_(data->ssl, rhs);

        if (status == this->ssl_error_want_read_ || status == this->ssl_error_want_write_) {
            /* force write all data */
            if (this->ssl_bio_flush_write_(conn, data, 1e5)) {
                return CONN_IO_ERROR;
            }

            if (this->flush_write_(conn, true))
                return CONN_IO_ERROR;
        }
        else
            return CONN_IO_ERROR;
    }

    if (ssl_is_init_fininshed_ (data->ssl)) {
        if (data->status & CONN_TLS_EARLY_DATA)
            data->status ^= CONN_TLS_EARLY_DATA;

        if (data->accept_timer) {
            data->accept_timer.stop();
            data->accept_timer.clear();
            data->accept_timer = nullptr;
        }

        if (data->status & CONN_TLS_SHUTDOWN)
            this->shutdown_async_(conn);

        return CONN_IO_OK;
    }
    return CONN_IO_AGAIN;
}

int manapi::net::worker::TLS::ssl_bio_flush_write_(const shared_conn &conn, TLS::connection_interface *m, int max_cnt) {
    int rhs;
    auto top = &m->top->send;

    char fastfast[65536];
    ssize_t nfastfast = 0;

    try {
        while (!(m->status & CONN_CLOSED)) {
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
                if (!m->top->send_size && rhs > 32) {
                    alr = m->watcher->try_write(fastfast, rhs);
                    if (alr < 0) {
                        if (alr == ev::ERR_AGAIN)
                            alr = 0;
                        else
                            /* fatal error */
                            return CONN_IO_ERROR;
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

int manapi::net::worker::TLS::ssl_bio_flush_read_(const shared_conn &conn, TLS::connection_interface *m, int max_cnt) {
    int rhs;
    auto top = &m->top->recv;

    char fastfast[65536];
    ssize_t nfastfast = 0;

    try {
        while (true) {
            if (max_cnt < m->top->recv_size)
                break;

            if (max_cnt < 10000)
                nfastfast = std::min<ssize_t>((max_cnt - m->top->recv_size + 1) * this->config_->buffer_size,
                    (sizeof (fastfast)));
            else
                nfastfast = sizeof (fastfast);

            if (m->status & CONN_TLS_EARLY_DATA) {
                std::size_t readbytes;
                auto early_res = this->ssl_read_early_data_(m->ssl, fastfast,
                    nfastfast, &readbytes);
                if (early_res == this->early_data_read_error_) {
                    rhs = 0;
                }
                else {
                    rhs = static_cast<int>(readbytes);

                    if (this->ssl_bio_flush_write_(conn, m, 1e5)) {
                        return CONN_IO_ERROR;
                    }

                    if (this->flush_write_(conn, true))
                        return CONN_IO_ERROR;

                    if (early_res == this->early_data_read_finish_) {
                        m->status |= CONN_TLS_EARLY_FINISHED;
                        auto const res = this->manapi_do_handshake_(conn, m);
                        switch (res) {
                            case CONN_IO_OK:
                                m->status ^= CONN_TLS_EARLY_DATA;
                                manapi_log_trace(debug::LOG_TRACE_LOW,
                                    "TLS:Early data was read %p", m);
                                break;
                            case CONN_IO_ERROR:
                                return CONN_IO_ERROR;
                            case CONN_IO_AGAIN:
                                break;
                            default:
                                return CONN_IO_ERROR;
                        }
                    }
                }
            }
            else
                rhs = this->ssl_read_(m->ssl, fastfast,
                static_cast<int>(nfastfast));

            if (rhs <= 0) {
                auto const err = this->ssl_get_error_(m->ssl, rhs);

                if (err == this->ssl_error_ssl_)
                    return CONN_IO_ERROR;

                if (err == this->ssl_error_syscall_)
                    break;

                if (err == this->ssl_error_want_read_ || err == this->ssl_error_want_write_) {
                    /* force write all data */
                    if (this->ssl_bio_flush_write_(conn, m, 1e5)) {
                        return CONN_IO_ERROR;
                    }

                    if (this->flush_write_(conn, true))
                        return CONN_IO_ERROR;
                }

                if (err == this->ssl_error_zero_return_)
                    this->close_connection(conn, CLOSE_CONN_SHUTDOWN);

                break;
            }

            if (!rhs)
                break;

            ssize_t alr = 0;
            if (!m->top->recv_size && (m->status & CONN_READ) && m->ev_callback) {
                if (this->call_user_callback(m->ev_callback, conn, ev::READ, fastfast, rhs, nullptr))
                    this->close_connection(conn, CLOSE_CONN_ERR);
            }
            else {
                TLS::connection_io_send(top, fastfast + alr, rhs - alr, &this->bufferpool(),
                    this->config_->buffer_size, &m->top->recv_size, 1e5);
                this->flush_read_(conn, m);
            }
        }
        return CONN_IO_OK;
    }
    catch (std::exception const &e) {
        manapi::async::current()->logger()->error(manapi::logger::default_service,
            manapi::ERR_INTERNAL, "TLS::ssl_bio_flush_read_(...): {}", e.what());
    }
    return CONN_IO_ERROR;
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
                if (this->call_user_callback(data->ev_callback, conn, ev::READ, object.data(),
                    static_cast<int>(object.size()), &object))
                    this->close_connection(conn, CLOSE_CONN_ERR);
            }
        }

        if (data->status & ((CONN_READ|CONN_CLOSED|CONN_REMOVED)) == CONN_READ) {
            this->read_start_(data);
        }

        return CONN_IO_OK;
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG("TlsOverTcp failed due to {}", e.what());
    }

    return CONN_IO_ERROR;
}