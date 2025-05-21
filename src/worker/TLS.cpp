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

    auto &sslconfig = this->config()->ssl_config();

    if (sslconfig.enabled) {
        // init
        this->ctx = this->ssl_create_context(this->config()->tls_version());
        // setup ctx (load certs)
        this->ssl_configure_context();
    }
}

void manapi::net::worker::TLS::configure_connection(const shared_conn & connection, oncont_cb cb) {
    auto conn = connection->as<connection_interface>();
    cb.call(true);
}

manapi::net::worker::shared_conn manapi::net::worker::TLS::accept(ev::shared_tcp &w) {
    auto connection = TCP::accept(w, [this] () -> shared_conn {
        auto ms = std::make_shared<net::worker::connection>(new connection_interface {}, connection_interface_eraser);
        auto connection = ms->as<connection_interface>();

        connection->ssl = this->ssl_new_(this->ctx);

        if (!recv_setup_connection (connection)) {
            return nullptr;
        }

        connection->accept_timer = manapi::async::current()->timerpool()->append_timer_sync(8000,
            [this, conn = ms.get()] (manapi::timer t) mutable
            -> void {
                auto connection = conn->as<connection_interface>();
                connection->status |= ev::DISCONNECT;
        });

        return std::move(ms);
    });

    connection->as<connection_interface>()->watcher->read_start();

    return std::move(connection);
}

void manapi::net::worker::TLS::close_connection(const shared_conn &conn, bool clean_disconnect) {
    auto connection = conn->as<connection_interface>();

    if (connection->accept_timer) {
        connection->accept_timer.stop();
        connection->accept_timer = nullptr;
    }

    if (connection->t) {
        connection->t.stop();
    }

    if (clean_disconnect) {


        return;
    }

    this->ssl_set_shutdown_(connection->ssl, this->ssl_send_shutdown_|this->ssl_recv_shutdown_);
}
#include "openssl/err.h"

ssize_t manapi::net::worker::TLS::sync_write_ex(const shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt) {
    auto connection = conn->as<connection_interface>();

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
            printf("%s\n", ERR_error_string(ERR_get_error(), NULL));
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
    return this->sync_write_ex(conn, buff, size, finish, this->config()->max_buffer_stack());
}

int manapi::net::worker::TLS::event_flags(const shared_conn & conn, int flags) {
    auto const data = conn->as<connection_interface>();
    auto &status = data->status;
    return std::exchange(status, ((status >> 2) << 2) | flags);
}

void manapi::net::worker::TLS::update_limit_rate_connection(connection *conn) {
    TCP::update_limit_rate_connection(conn);
}

void manapi::net::worker::TLS::connection_interface_eraser(void *ptr) {
    auto connection = static_cast<connection_interface *> (ptr);

    if (connection->ssl) {
        auto ssl = std::exchange(connection->ssl, nullptr);
        std::cout << ("SSL FREE\n");
        (dynamic_cast<TLS*>(connection->worker.get()))->ssl_free_(ssl);
    }

    delete connection;
}

void manapi::net::worker::TLS::onrecv(std::shared_ptr<ev::tcp> &watcher, const shared_conn &conn, ibuffpool_t buffer) {
    auto buff = buffer->as<char>();
    auto size = static_cast<ssize_t>(buffer->size());
    auto data = conn->as<TLS::connection_interface>();

    while (size) {
        auto rhs = this->ssl_bio_write_(data->rbio, buff, static_cast<int>(size));
        if (rhs <= 0) {
            /* error */
            goto err;
        }

        buff += rhs;
        size -= rhs;

        if (ssl_is_init_fininshed_(data->ssl)) {
            if (auto const res = this->ssl_bio_flush_read_(conn, data->wbio, &data->top->recv, &data->top->recv_size, 1e5)) {
                if (res == CONN_IO_ERROR)
                    goto err;
            }
        }
        else {
            rhs = this->ssl_accept_(data->ssl);
            auto const status = this->ssl_get_error_(data->ssl, rhs);

            if (status == this->ssl_error_want_write_ || status == this->ssl_error_want_read_) {
                /* force write all data */
                if (this->ssl_bio_flush_write_(conn, data->wbio, &data->top->send, &data->top->send_size, 1e5)) {
                    goto err;
                }

                this->flush_write_(conn, true);
            }
            else if (status) {
                goto err;
            }
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
        conn->as<connection_interface>()->status |= ev::DISCONNECT;
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
//     conn->as<connection_interface>()->watcher->read_start();
// }

void manapi::net::worker::TLS::accept_work_(const shared_conn &conn, int flags, ibuffpool_t buffer) {
    auto buff = buffer->as<char>();
    auto size = static_cast<ssize_t>(buffer->size());

    auto data = conn->as<connection_interface>();

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
            ERR_SSL_CONNECTION, "TLS: accept_work_(): {}", e.what());
    }
    err: {
        this->close_connection(conn, false);
        return;
    }
    finish: {
        auto http_v1_1_ctx = std::make_unique<http::http_v1_1_t>();
        this->event_on(conn,
            std::make_unique<worker_watcher_cb>([this, http_v1_1_ctx = std::move(http_v1_1_ctx)]
            (const shared_conn &conn, int flags, const char *buffer, ssize_t nsize) mutable
            -> void {
                this->http_work_ (http_v1_1_ctx.get(), conn, flags, buffer, nsize);
        }));
    }
}

void manapi::net::worker::TLS::flush_write_(const shared_conn &connection, bool flush) {
    auto const data = connection->as<connection_interface>();
    if (data->top->send_size == 1 && data->top->send.last_deque) {
        /* in the stack */
        auto &buffer = data->top->send.deque->buffer;
        auto copy = static_cast<int>(data->top->send.deque_cursor - data->top->send.deque_current);
        auto const rhs = data->watcher->try_write(buffer->data() + data->top->send.deque_current, copy);
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
    if (data->top->recv_size > this->config()->max_buffer_stack()) {
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

    do {
        if (!top->last_deque || top->last_deque->buffer->size() == top->deque_cursor) {
            this->flush_write_(conn, false);

            if (cnt && *cnt >= max_cnt)
                return CONN_IO_WANT_WRITE;

            auto buffer = this->bufferpool()->get();
            buffer->resize_max(this->config_->buffer_size());

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

        rhs = this->ssl_bio_read_(wbio, top->last_deque->buffer->data() + top->deque_cursor,
            static_cast<int>(top->last_deque->buffer->size() - top->deque_cursor));

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

int manapi::net::worker::TLS::ssl_bio_flush_read_(const shared_conn &conn, void *rbio, connection_io_part *top, int *cnt, int max_cnt) {
    int rhs;
    int err;
    int flags = 0;
    buffer_deque *parent = nullptr;
    auto data = conn->as<connection_interface>();

    do {
        if (!top->last_deque || top->last_deque->buffer->size() == top->deque_cursor) {
            if (auto const res = ssl_flush_recv(conn, top, cnt)) {
                return res;
            }

            if (this->check_read_stack_full_(conn->as<connection_interface>())) {
                return CONN_IO_ERROR;
            }

            if (cnt && *cnt >= max_cnt)
                return CONN_IO_WANT_READ;

            auto buffer = this->bufferpool()->get();
            buffer->resize_max(this->config_->buffer_size());

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

        rhs = this->ssl_read_(data->ssl, top->last_deque->buffer->data() + top->deque_cursor,
            static_cast<int>(top->last_deque->buffer->size() - top->deque_cursor));

        if (rhs >= 0) {
            top->deque_cursor += rhs;
            if (!rhs && (flags /* an empty buffer was created */ )) {
                /* remove an empty buffer at the end */
                connection_io_trim(top, parent, cnt);
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
    auto data = conn->as<connection_interface>();

    try {
        while (top->last_deque) {
            auto object = std::move(top->deque->buffer);
            top->deque = std::move(top->deque->next);

            if (!top->deque) {
                object->resize(top->deque_cursor);
                top->last_deque = nullptr;
                top->deque_cursor = 0;
            }

            if (top->deque_current) {
                object->shift_add(top->deque_current);
                top->deque_current = 0;
            }

            if (cnt)
                (*cnt)--;

            data->ev_callback->operator()(conn, ev::READ, object->data(), object->size() /**,std::move(object)**/);
        }

        return CONN_IO_OK;
    }
    catch (std::exception const &e) {
        std::cerr << e.what() << "\n";
    }

    return CONN_IO_ERROR;
}
