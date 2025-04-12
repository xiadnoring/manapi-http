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

#define WANT_READ(x_, ctx) x_.status.fetch_or(CONN_READ); \
x_.iocancel.reset(ctx);\
x_.iocancel.ask_cancel_callback();\
x_.iocancel.handle_ready([&] () -> void { x_.iomutex.unlock(); unlock.disable(); lk.call(); }); \
co_await async::read_ready (this->site.async_context(), x_.id, x_.iocancel); \
x_.status.fetch_xor(CONN_READ);
#define WANT_WRITE(x_, ctx) x_.status.fetch_or(CONN_WRITE); \
x_.iocancel.reset(ctx);\
x_.iocancel.ask_cancel_callback();\
x_.iocancel.handle_ready([&] () -> void { x_.iomutex.unlock(); unlock.disable(); lk.call();  }); \
co_await async::write_ready (this->site.async_context(), x_.id, x_.iocancel); \
x_.status.fetch_xor(CONN_WRITE);

manapi::net::worker::TLS::TLS(net::site &site) : TCP(site) {
}

manapi::net::worker::TLS::~TLS() = default;

bool manapi::net::worker::TLS::is_valid_connection(worker::connection &connection) {
    return connection.as<connection_interface>().id >= 0;
}

void manapi::net::worker::TLS::init() {
    TCP::init();

    auto sslconfig = this->config->get_ssl_config();
    if (sslconfig->enabled) {
        // init
        this->ctx = ssl_create_context(this->config->get_tls_version());
        // setup ctx (load certs)
        ssl_configure_context();

        this->write = [this](auto &PH1, auto PH2, auto PH3, auto PH4)
            -> future<ssize_t> { return ssl_write(PH1, PH2, PH3); };

        this->read = [this](auto &PH1, auto PH2, auto PH3)
            -> future<ssize_t> { return ssl_read(PH1, PH2, PH3); };
    }
}

manapi::future<bool> manapi::net::worker::TLS::configure_connection(std::shared_ptr<connection> connection) {
    auto &conn = connection->as<connection_interface>();

    if (conn.configured) {
        co_return true;
    }

    if (!this->ssl_is_init_fininshed_(conn.ssl)) {
        auto lk = co_await conn.mx->lock_guard();

        if (!conn.iomutex.try_to_lock()) {
            co_return -1;
        }

        auto unlock = before_delete ([&] ()
            -> void { conn.iomutex.unlock(); });

        while (true) {
            if (conn.status & CONN_CLOSED) {
                co_await conn.accept_timer.async_stop(this->site.async_context());
                co_return false;
            }

            int rhs = this->ssl_accept_(conn.ssl);
            rhs = this->ssl_get_error_(conn.ssl, rhs);

            if (rhs != this->ssl_error_none_) {
                if (rhs == this->ssl_error_want_read_) {
                    WANT_READ(conn, this->site.async_context());
                }
                else if (rhs == this->ssl_error_want_read_) {
                    WANT_WRITE(conn, this->site.async_context());
                }
                else if (rhs == this->ssl_error_zero_return_) {
                    conn.iomutex.unlock();
                    unlock.disable();

                    co_await conn.accept_timer.async_stop(this->site.async_context());
                    co_await this->connection_close(std::move(connection), true);
                    co_return false;
                }
                else {
                    conn.iomutex.unlock();
                    unlock.disable();

                    co_await conn.accept_timer.async_stop(this->site.async_context());
                    co_await this->connection_close(std::move(connection), false);
                    co_return false;
                }

                lk = co_await conn.mx->lock_guard();
                if (!conn.iomutex.try_to_lock()) {
                    co_return false;
                }

                unlock.enable();
            }

            if (this->ssl_is_init_fininshed_(conn.ssl)) {
                break;
            }
        }

        co_await conn.accept_timer.async_stop(this->site.async_context());
    }

    conn.status.fetch_xor(CONN_IDLE);
    conn.configured = true;

    co_return true;
}

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::TLS::accept() {
    auto connection = TCP::accept([this] () {
        auto ms = std::make_shared<net::worker::connection>(new connection_interface {this->site.async_context()}, connection_interface_eraser);
        auto &connection = ms->as<connection_interface>();
        connection.ssl = this->config->get_ssl_config()->enabled ? this->ssl_new_(this->ctx) : nullptr;
        connection.mx = std::make_unique<async::mutex>(this->site.async_context());
        connection.accept_timer = this->site.async_context()->timerpool()->append_timer_sync(8000, [this, ms] (manapi::timer t)
            -> void {
            async::run(this->site.async_context(), this->connection_close(ms, false));
        });
        return std::move(ms);
    });

    return std::move(connection);
}

manapi::future<> manapi::net::worker::TLS::connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) {
    if (!conn) {
        co_return;
    }

    auto &connection = conn->as<connection_interface>();
    auto lk = co_await connection.iomutex.lock_guard();


    if (connection.iocancel) {
        co_await connection.iocancel.cancel();
        connection.iocancel = nullptr;
    }

    if ((false == connection.status & CONN_CLOSED)) {
        if (clean_disconnect) {
            bool flag = true;

            auto timer = co_await this->site.async_context()->timerpool()->async_append_timer_sync(1000, [&] (manapi::timer t)
                -> void {
                /* libev loop */
                if (connection.iocancel) {
                    connection.iocancel.sync_cancel();
                }
            });

            do {
                auto rhs = this->ssl_shutdown_(connection.ssl);
                int ssl_errno = this->ssl_get_error_(connection.ssl, rhs);
                if (ssl_errno == this->ssl_error_none_) {
                    break;
                }
                if (ssl_errno == this->ssl_error_want_read_) {
                    connection.iocancel.reset(this->site.async_context());
                    connection.iocancel.ask_cancel_callback();
                    co_await async::read_ready(this->site.async_context(), connection.id, connection.iocancel);
                }
                else if (ssl_errno == ssl_error_want_write_) {
                    connection.iocancel.reset(this->site.async_context());
                    connection.iocancel.ask_cancel_callback();
                    co_await async::write_ready(this->site.async_context(), connection.id, connection.iocancel);
                }
                else if (ssl_errno == this->ssl_error_syscall_) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        connection.iocancel.reset(this->site.async_context());
                        connection.iocancel.ask_cancel_callback();
                        co_await async::read_ready(this->site.async_context(), connection.id, connection.iocancel);
                    }
                    else {
                        flag=false;
                    }
                }
                else {
                    flag = false;
                }
            }
            while (flag);

            co_await timer.async_stop(this->site.async_context()->eventloop());
        }
        else {
            this->ssl_set_shutdown_(connection.ssl, this->ssl_send_shutdown_|this->ssl_recv_shutdown_);
        }
    }

    this->_connection_close(conn, connection);
}

int manapi::net::worker::TLS::status(connection &conn) {
    return conn.as<connection_interface>().status;
}

ssize_t manapi::net::worker::TLS::sync_read(worker::connection *conn, void *buff, ssize_t size) {
    auto &connection = conn->as<connection_interface>();

    auto rhs = std::min(this->config->speed_limit_rate().load() - connection.stats.transfared_last_second.load(), size);
    if (rhs <= 0 && size) {
        if (connection.watcher) { connection.watcher->stop(); }
        connection.status.fetch_or(CONN_LIMIT_RATE);
        return 0;
    }


    rhs = this->ssl_read_(connection.ssl, buff, static_cast<int>(rhs));
    auto ssl_errno = this->ssl_get_error_(connection.ssl, static_cast<int>(rhs));

    if (rhs < 0) {
        if (ssl_errno == this->ssl_error_syscall_) {
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? (IO_WANT_AGAIN) : (-1);
        }
        if (ssl_errno == this->ssl_error_want_read_) {
            return IO_WANT_READ;
        }
        if (ssl_errno == this->ssl_error_want_write_) {
            return IO_WANT_WRITE;
        }

        return IO_FATAL_ERROR;
    }


    connection.stats.transfared_last_second.fetch_add(rhs);
    connection.stats.total_read.fetch_add(rhs);

    return rhs;
}

ssize_t manapi::net::worker::TLS::sync_write(worker::connection *conn, const void *buff, ssize_t size) {
    auto &connection = conn->as<connection_interface>();

    auto rhs = std::min(this->config->speed_limit_rate().load() - connection.stats.transfared_last_second.load(), size);
    if (rhs <= 0 && size) {
        if (connection.watcher) { connection.watcher->stop(); }
        connection.status.fetch_or(CONN_LIMIT_RATE);
        return 0;
    }

    rhs = this->ssl_write_(connection.ssl, buff, static_cast<int>(size));
    auto ssl_errno = this->ssl_get_error_(connection.ssl, static_cast<int>(rhs));

    if (rhs < 0) {
        if (ssl_errno == this->ssl_error_syscall_) {
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? (IO_WANT_AGAIN) : (-1);
        }
        if (ssl_errno == this->ssl_error_want_read_) {
            return IO_WANT_READ;
        }
        if (ssl_errno == this->ssl_error_want_write_) {
            return IO_WANT_WRITE;
        }
        return IO_FATAL_ERROR;
    }

    connection.stats.transfared_last_second.fetch_add(rhs);
    connection.stats.total_write.fetch_add(rhs);

    return rhs;
}

manapi::future<std::shared_ptr<ev::io>> manapi::net::worker::TLS::async_watch_io(worker::connection *conn, int revents,
    std::move_only_function<void(ev::io &w, int revents)> callback) {
    auto &connection = conn->as<connection_interface>();
    if (!connection.watcher) {
        connection.watcher = co_await this->site.async_context()->eventloop()->watch_fd(conn->as<connection_interface>().id, revents, std::move(callback));
    }
    co_return connection.watcher;
}

std::shared_ptr<ev::io> manapi::net::worker::TLS::sync_watch_io(worker::connection *conn, int revents,
    std::move_only_function<void(ev::io &w, int revents)> move_only_function) {
    auto &connection = conn->as<connection_interface>();
    if (!connection.watcher) {
        connection.watcher = this->site.async_context()->eventloop()->create_watcher_fd(connection.id, revents, std::move(move_only_function));
        connection.watcher->start();
    }
    return connection.watcher;
}

void manapi::net::worker::TLS::recv_setup_connection(manapi::net::worker::connection &storage) {}

void manapi::net::worker::TLS::update_limit_rate_connection(connection &conn) {
    auto &conn_data = conn.as<connection_interface>();
    conn_data.stats.transfared_last_second.store(0);
    const auto flag = conn_data.status.load();
    if (conn_data.watcher && !(flag & CONN_CLOSED)) { conn_data.watcher->start(); }
    if (flag & CONN_LIMIT_RATE) { conn_data.status.fetch_xor(CONN_LIMIT_RATE); }
}

void manapi::net::worker::TLS::connection_interface_eraser(void *ptr) {
    auto connection = static_cast<connection_interface *> (ptr);
    async::run(connection->worker->site.async_context(), [connection] () mutable -> future<void> {
        TCP::_connection_interface_eraser (connection);
        auto prev = connection;
        if (connection->ssl) {
            auto ssl = std::exchange(connection->ssl, nullptr);
            //MANAPIHTTP_LOG("SSL FREE: {}", connection->id);
            (dynamic_cast<TLS*>(connection->worker.get()))->ssl_free_(ssl);
        }
        std::cerr << "SSL CLOSED: " << connection->id <<"\n";
#ifdef _WIN32
        ::closesocket(connection->id);
#else
        ::close(connection->id);
#endif
        delete connection;
        co_return;
    });
}

bool manapi::net::worker::TLS::ssl_is_init_fininshed_ (void *ssl) {
    return false;
}

int manapi::net::worker::TLS::ssl_get_error_(void *ssl, int rhs) {
    return 0;
}

int manapi::net::worker::TLS::ssl_accept_(void *ssl) {
    return 0;
}

void * manapi::net::worker::TLS::ssl_new_(void *ctx) {
    return nullptr;
}

int manapi::net::worker::TLS::ssl_write_(void *ssl, const void *buff, int size) {
    return 0;
}

int manapi::net::worker::TLS::ssl_read_(void *ssl, void *buff, int size) {
    return 0;
}

int manapi::net::worker::TLS::ssl_shutdown_(void *ssl) {
    return 0;
}

void manapi::net::worker::TLS::ssl_set_shutdown_(void *ssl, int flags) {

}

void manapi::net::worker::TLS::ssl_free_(void *ssl) {

}

void * manapi::net::worker::TLS::ssl_create_context(const size_t &version) {
    return nullptr;
}

void manapi::net::worker::TLS::ssl_configure_context() {

}

manapi::future<ssize_t> manapi::net::worker::TLS::ssl_write(connection &conn, const void *buff, ssize_t size) {
    auto &connection = conn.as<connection_interface>();


    auto lk = co_await connection.mx->lock_guard();

    if (!connection.iomutex.try_to_lock()) {
        co_return -1;
    }
    auto unlock = before_delete ([&] ()
        -> void { connection.iomutex.unlock(); });

    const auto limit_rate = this->config->speed_limit_rate().load();

    while (true) {
        int rhs;
        int ssl_errno = this->ssl_error_none_;

        if (connection.status & CONN_CLOSED) {
            break;
        }

        if (connection.stats.transfared_last_second >= limit_rate) {
            unlock.disable();
            connection.iomutex.unlock();
            lk.call();
            connection.status.fetch_or(CONN_LIMIT_RATE);
            co_await this->limit_rate_cv->wait([&] ()
                -> bool { return connection.status & CONN_CLOSED || connection.stats.transfared_last_second < limit_rate; });
            lk = co_await connection.mx->lock_guard();
            if (!connection.iomutex.try_to_lock()) {
                co_return -1;
            }

            unlock.enable();
        }

        size = std::min(limit_rate - connection.stats.transfared_last_second.load(), size);

        rhs = this->ssl_write_(connection.ssl, buff, static_cast<int>(size));
        ssl_errno = this->ssl_get_error_(connection.ssl, static_cast<int>(rhs));

        if (rhs < 0) {
            // if((err = SSL_get_error(SSL*,err)) == SSL_ERROR_ZERO_RETURN)


            if (ssl_errno != this->ssl_error_none_) {
                if (ssl_errno == this->ssl_error_syscall_) {
                    co_return -1;
                }

                if (ssl_errno == this->ssl_error_want_read_) {
                    WANT_READ(connection, this->site.async_context());
                }
                else if (ssl_errno == this->ssl_error_want_write_) {
                    WANT_WRITE(connection, this->site.async_context());
                }
                else {
                    co_return -1;
                }

                lk = co_await connection.mx->lock_guard();
                if (!connection.iomutex.try_to_lock()) {
                    co_return -1;
                }

                unlock.enable();
                continue;
            }

            break;
        }

        connection.stats.transfared_last_second.fetch_add(rhs);
        connection.stats.total_write.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}

manapi::future<ssize_t> manapi::net::worker::TLS::ssl_read(connection &conn, void *buff, ssize_t size) {
    auto &connection = conn.as<connection_interface>();

    auto lk = co_await connection.mx->lock_guard();

    if (!connection.iomutex.try_to_lock()) {
        co_return -1;
    }
    auto unlock = before_delete ([&] ()
        -> void { connection.iomutex.unlock(); });

    const auto limit_rate = this->config->speed_limit_rate().load();

    while (true) {
        int rhs;
        int ssl_errno = this->ssl_error_none_;

        if (connection.status & CONN_CLOSED) {
            break;
        }

        if (connection.stats.transfared_last_second >= limit_rate) {
            unlock.disable();
            connection.iomutex.unlock();
            lk.call();

            connection.status.fetch_or(CONN_LIMIT_RATE);
            co_await this->limit_rate_cv->wait([&] ()
                -> bool { return connection.status & CONN_CLOSED || connection.stats.transfared_last_second < limit_rate; });

            lk = co_await connection.mx->lock_guard();

            if (!connection.iomutex.try_to_lock()) {
                co_return -1;
            }

            unlock.enable();
        }

        size = std::min(limit_rate - connection.stats.transfared_last_second.load(), size);

        // if(SSL_get_shutdown(SSL*) & SSL_RECEIVED_SHUTDOWN)
        rhs = this->ssl_read_(connection.ssl, buff, static_cast<int>(size));
        ssl_errno = this->ssl_get_error_(connection.ssl, static_cast<int>(rhs));

        if (rhs < 0) {

            if (ssl_errno != this->ssl_error_none_) {
                if (ssl_errno == this->ssl_error_syscall_) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        WANT_READ(connection, this->site.async_context());
                    }
                    else {
                        co_return -1;
                    }
                }
                else if (ssl_errno == this->ssl_error_want_read_) {
                    WANT_READ(connection, this->site.async_context());
                }
                else if (ssl_errno == this->ssl_error_want_write_) {
                    WANT_WRITE(connection, this->site.async_context());
                }
                else if (ssl_errno == this->ssl_error_zero_return_) {
                    co_return 0;
                }
                else {
                    co_return -1;
                }

                lk = co_await connection.mx->lock_guard();

                if (!connection.iomutex.try_to_lock()) {
                    co_return -1;
                }

                unlock.enable();
                continue;
            }
            break;
        }

        connection.stats.transfared_last_second.fetch_add(rhs);
        connection.stats.total_read.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}
