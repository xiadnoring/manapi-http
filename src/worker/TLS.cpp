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

    auto &sslconfig = this->config_->ssl_config;

    if (sslconfig.enabled) {
        // init
        this->ctx = this->ssl_create_context(this->config_->tls_version);
        // setup ctx (load certs)
        this->ssl_configure_context();
    }
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

        if (!recv_setup_connection (connection)) {
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

ssize_t manapi::net::worker::TLS::sync_write_ex(const shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt) {
    auto connection = conn->as<TLS::connection_interface>();

    if (connection->status & ev::DISCONNECT) {
        return -1;
    }

    ssize_t res = 0;

    while (res != size) {
        if (connection->top->send_size >= maxcnt) {
            return res;
        }

        auto rhs = this->ssl_write_(connection->ssl, static_cast<const char *> (buff) + res, static_cast<int>(size - res));

        if (rhs >= 0) {
            res += rhs;
            connection->transfered += rhs;
        }

        auto err = this->ssl_get_error_(connection->ssl, rhs);

        if (err == this->ssl_error_none_) {

        }

        else if (err == this->ssl_error_want_read_) {
            return res;
        }

        else if (err == this->ssl_error_want_write_) {
            err = this->ssl_bio_flush_write_(conn, connection->wbio,
                &connection->top->send, &connection->top->send_size, maxcnt);

            if (err) {
                if (err == CONN_IO_WANT_WRITE) {
                    this->flush_write_(conn, finish);
                    return res;
                }

                return CONN_IO_ERROR;
            }

            this->flush_write_(conn, true);

            continue;
        }
        else {
            return CONN_IO_ERROR;
        }

        err = this->ssl_bio_flush_write_(conn, connection->wbio,
                &connection->top->send, &connection->top->send_size, maxcnt);

        if (err) {
            if (err == CONN_IO_WANT_WRITE) {
                this->flush_write_(conn, finish);
                return res;
            }

            return CONN_IO_ERROR;
        }

        this->flush_write_(conn, finish);
    }

    return res;
}

ssize_t manapi::net::worker::TLS::sync_write(const shared_conn &conn, const void *buff, ssize_t size, bool finish) {
    auto const data = conn->as<TLS::connection_interface>();
    size = std::min(size, this->config_->speed_limit_rate - data->transfered);

    if (size <= 0) {
        return size;
    }

    return this->sync_write_ex(conn, buff, size, finish, static_cast<int>(this->config_->max_buffer_stack));
}

int manapi::net::worker::TLS::event_flags(const shared_conn & conn, int flags) noexcept(true) {
    auto const data = conn->as<TLS::connection_interface>();
    auto &status = data->status;
    data->speed_min_delay = static_cast<int>(this->config_->speed_check_delay);
    auto const prev = std::exchange(status, ((status >> 2) << 2) | (flags & CONN_MASK_UPDATE));

    if (status & ev::READ & flags) {
        if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
            this->global_.flush_custom_read_cb(conn, &this->global_, this);

        flush_read_ (conn, data);

        if (!(status & (CONN_CLOSED|CONN_REMOVED)) && !data->watcher->is_active()) {
            data->watcher->read_start();
        }
    }

    if ((status & CONN_RECV_END) && (status & CONN_READ & flags) && data->ev_callback) {
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

    wrk->count--;
    wrk->worker_data()->count.fetch_sub(1);

    if (wrk->flags & NET_WORKER_CLOSED
        && !wrk->count
        && wrk->finish) {
        wrk->finish();
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
                    if (this->ssl_bio_flush_write_(conn, data->wbio, &data->top->send, &data->top->send_size, 1e5)) {
                        goto err;
                    }

                    this->flush_write_(conn, true);
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

void manapi::net::worker::TLS::accept_work_(const shared_conn &conn, int flags, ibuffpool_t buffer) {
    auto buff = buffer.as<char>();
    auto size = static_cast<ssize_t>(buffer.size());

    auto data = conn->as<TLS::connection_interface>();

    try {
        while (size) {
            int rhs = this->ssl_bio_write_(data->rbio, buff, static_cast<int>(size));
            int status;

            if (rhs <= 0) {
                /* error */
                goto err;
            }

            buff += rhs;
            size -= rhs;

            if (this->ssl_is_init_fininshed_(data->ssl)) {
                goto finish;
            }
            else {
                rhs = this->ssl_accept_(data->ssl);
                status = this->ssl_get_error_(data->ssl, rhs);

                if (status == this->ssl_error_want_write_ || status == this->ssl_error_want_read_) {
                    /* force write all data */
                    if (this->ssl_bio_flush_write_(conn, data->wbio, &data->top->send, &data->top->send_size, 1e5)) {
                        goto err;
                    }

                    this->flush_write_(conn, true);
                }
                else {
                    goto err;
                }

                if (this->ssl_is_init_fininshed_(data->ssl)) {
                    goto finish;
                }
            }
        }
        return;
    }
    catch (std::exception const &e) {
        manapi::async::current()->logger()->error(manapi::logger::default_service,
            ERR_FAILED_PRECONDITION, "TLS: accept_work_(): {}", e.what());
    }
    err: {
        this->close_connection(conn, false);
        return;
    }
    finish: {
        auto http_v1_1_ctx = std::make_unique<http::http_v1_1_t>();
        this->event_on(conn,
            std::make_unique<worker_watcher_cb>([this, http_v1_1_ctx = std::move(http_v1_1_ctx)]
            (const shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) mutable
            -> void {
                auto const data = conn->as<TLS::connection_interface>();
                this->global_.accept_cb (conn, flags, buffer, nsize, p,
                    &this->global_, this);
        }));
        this->event_flags(conn, ev::READ);
    }
}

void manapi::net::worker::TLS::flush_write_(const shared_conn &connection, bool flush) {
    auto const data = connection->as<TLS::connection_interface>();
    if (data->top->send_size == 1 && data->top->send.last_deque) {
        /* in the stack */
        auto &buffer = data->top->send.deque->buffer;
        auto copy = static_cast<int>(data->top->send.deque_cursor - data->top->send.deque_current);
        auto const rhs = data->watcher->try_write(buffer.data() + data->top->send.deque_current, copy);
        if (rhs >= 0) {
            if (rhs == copy) {
                data->top->send.deque = nullptr;
                data->top->send.last_deque = nullptr;
                data->top->send.deque_current = 0;
                data->top->send.deque_cursor = 0;
                data->top->send_size--;
                return;
            }

            data->top->send.deque_current += static_cast<int>(rhs);
        }
    }

    TCP::flush_write_(connection, flush);
}

int manapi::net::worker::TLS::check_read_stack_full_(connection_interface *data) {
    if (data->top->recv_size >= this->config_->max_buffer_stack) {
        /* sadness */
        if (auto rhs = data->watcher->read_stop()) {
            return rhs;
        }
    }
    return 0;
}

int manapi::net::worker::TLS::ssl_bio_flush_write_(const shared_conn &conn, void *wbio, connection_io_part *top, int *cnt, int max_cnt) {
    int rhs;
    int flags = 0;
    buffer_deque *parent = nullptr;

    try {
        do {
            if (!top->last_deque || top->last_deque->buffer.size() == top->deque_cursor) {
                this->flush_write_(conn, false);

                if (cnt && *cnt >= max_cnt)
                    return CONN_IO_WANT_WRITE;

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
                flags |= 1 /* an empty buffer was created */;
                if (cnt)
                    (*cnt)++;
            }
            else
                flags = 0;

            rhs = this->ssl_bio_read_(wbio, top->last_deque->buffer.data() + top->deque_cursor,
                static_cast<int>(top->last_deque->buffer.size() - top->deque_cursor));

            if (rhs > 0) {
                top->deque_cursor += rhs;
            }
            else {
                if (!this->ssl_bio_should_retry_(wbio)) {
                    return CONN_IO_ERROR;
                }
            }

            if (!rhs && (flags /* an empty buffer was created */ )) {
                /* remove an empty buffer at the end */
                connection_io_trim(top, parent, cnt);
            }
        }
        while (rhs > 0);

        return CONN_IO_OK;
    }
    catch (std::exception const &e) {
        manapi::async::current()->logger()->error(manapi::logger::default_service,
            manapi::ERR_INTERNAL, "TLS::ssl_bio_flush_write_(...): {}", e.what());
    }

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
            && data->status & ev::READ) {
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

        if (data->status & ev::READ
            && !(data->status & (CONN_CLOSED|CONN_REMOVED))
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