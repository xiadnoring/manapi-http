#include "worker/OpenSSL_TLS.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY

#include <iostream>
#include <csignal>
#include <utility>
#include <vector>
#include <memory.h>
#include <arpa/inet.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <netdb.h>
#include <set>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "ManapiUtils.hpp"
#include "http/HeaderView.hpp"

std::atomic<bool> manapi::net::worker::OpenSSL_TLS::gl_init = false;

void ssl_library_init (std::atomic<bool> &gl_init) {
    OpenSSL_add_ssl_algorithms();
    SSL_load_error_strings();
    SSLeay_add_ssl_algorithms();
    gl_init.store(true);
}

manapi::net::worker::OpenSSL_TLS::OpenSSL_TLS(net::site &site) : TCP (site) {
    if (!gl_init) {
        ssl_library_init (gl_init);
    }
}

manapi::net::worker::OpenSSL_TLS::~OpenSSL_TLS() {
    SSL_CTX_free(ctx);
}

bool manapi::net::worker::OpenSSL_TLS::is_valid_connection(worker::connection &connection) {
    return connection.as<connection_interface>().id >= 0;
}

void manapi::net::worker::OpenSSL_TLS::init() {
    TCP::init();

    auto sslconfig = config->get_ssl_config();
    if (sslconfig->enabled) {
        // init
        ctx = ssl_create_context(config->get_tls_version());
        // setup ctx (load certs)
        ssl_configure_context();

        this->write = [this](auto &PH1, auto PH2, auto &PH3, auto PH4) -> future<ssize_t> {
            const auto rhs = co_await ssl_write(PH1, PH2, PH3);
            co_return rhs;
        };

        this->read = [this](auto &PH1, auto PH2, auto &PH3) -> future<ssize_t> {
            const auto rhs = co_await ssl_read(PH1, PH2, PH3);
            co_return rhs;
        };
    }
}

manapi::net::future<bool> manapi::net::worker::OpenSSL_TLS::configure_connection(std::shared_ptr<connection> connection) {
    auto &conn = connection->as<connection_interface>();

    if (conn.configured) { co_return true; }

    if (!SSL_is_init_finished(conn.ssl)) {
        conn.mustly.fetch_xor(CONN_READ | CONN_WRITE);
        conn.timer_accept.store(this->site.append_timer(std::chrono::milliseconds(2000), [this, conn = &conn, connection] () -> void {
            conn->timer_accept.store(0);
            MANAPIHTTP_LOG("TIMEOUT SSL_ACCEPT: {}", conn->id);
            async::task_run(this->site.taskpool, this->connection_close(connection));
        }));

        while (true) {
            if (conn.status & CONN_CLOSED) {
                this->site.remove_timer(conn.timer_accept);
                conn.timer_accept.store(0);
                co_return false;
            }

            MANAPIHTTP_LOG("SSL ACCEPT: {}", conn.id);

            int rhs = SSL_accept(conn.ssl);
            rhs = SSL_get_error(conn.ssl, rhs);
            if (rhs != SSL_ERROR_NONE) {
                switch (rhs) {
                    case SSL_ERROR_WANT_READ: {
                        co_await manapi::net::worker::OpenSSL_TLS::io_wait(conn, CONN_READ);
                        continue;
                    }
                    case SSL_ERROR_WANT_WRITE: {
                        co_await manapi::net::worker::OpenSSL_TLS::io_wait(conn, CONN_WRITE);
                        continue;
                    }
                    case SSL_ERROR_WANT_ASYNC:
                        MANAPIHTTP_LOG2("WANT ASYNC");
                    continue;
                    case SSL_ERROR_SSL:
                        MANAPIHTTP_LOG("SSL_ERROR_SSL: fd: {}", conn.id);
                    goto error;
                    case SSL_ERROR_SYSCALL:
                        MANAPIHTTP_LOG("SSL_ERROR_SYSCALL: {}, fd: {}", errno, conn.id);
                    goto error;
                    default:
                        MANAPIHTTP_LOG("SSL_accept(...), rhs = {}", rhs);
                    error:
                        co_await this->connection_close(connection);
                    co_return false;

                }
            }

            if (SSL_is_init_finished(conn.ssl)) {
                break;
            }
        }

        conn.site->remove_timer(conn.timer_accept);
        conn.timer_accept.store(0);
        conn.mustly.fetch_or(CONN_READ | CONN_WRITE);
    }


    conn.status.fetch_xor(CONN_IDLE);
    conn.configured = true;

    co_return true;
}

manapi::net::worker::OpenSSL_TLS & manapi::net::worker::OpenSSL_TLS::operator=(OpenSSL_TLS &&n) noexcept {
    TCP::operator=(std::forward<worker::TCP>(n));
    return *this;
}

void manapi::net::worker::OpenSSL_TLS::disable_watcher_for_status(connection &conn, const connection_status &status) {
    auto &conn_data = conn.as<connection_interface>();
    conn_data.mustly.fetch_xor(status);
}

std::shared_ptr<manapi::net::worker::OpenSSL_TLS> manapi::net::worker::OpenSSL_TLS::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::OpenSSL_TLS>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::OpenSSL_TLS::accept() {
    auto connection = TCP::accept([this] () {
        auto ms = std::make_shared<worker::connection> (new connection_interface {this->site.taskpool}, connection_interface_eraser);
        auto &connection = ms->as<connection_interface>();
        connection.handle = [this] (auto &&P1, auto &&P2) -> void {
            this->_io_event(std::forward<decltype(P1)>(P1), std::forward<decltype(P2)>(P2));
        };
        connection.ssl = this->config->get_ssl_config()->enabled ? SSL_new(this->ctx) : nullptr;
        connection.wmx = std::make_unique<async_mutex>(this->site.taskpool);
        connection.rmx = std::make_unique<async_mutex>(this->site.taskpool);
        return std::move(ms);
    });

    return std::move(connection);
}

manapi::net::future<void> manapi::net::worker::OpenSSL_TLS::connection_close(std::shared_ptr<connection> conn) {
    auto &connection = conn->as<connection_interface>();
    auto lk = co_await connection.iomutex.lock_guard();

    if (false == connection.status & CONN_CLOSED) {
        bool flag = true;
        do {
            auto rhs = SSL_shutdown(connection.ssl);
            int ssl_errno = SSL_get_error(connection.ssl, rhs);
            if (ssl_errno == SSL_ERROR_NONE) {
                break;
            }
            switch (ssl_errno) {
                case SSL_ERROR_WANT_READ:
                    co_await io_wait(connection, CONN_READ);
                break;
                case SSL_ERROR_WANT_WRITE:
                    co_await io_wait(connection, CONN_WRITE);
                break;
                case SSL_ERROR_SYSCALL: {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        co_await io_wait(connection, CONN_READ);
                        break;
                    }
                }
                default:
                    flag = false;
                    break;
            }
        } while (flag);
    }

    this->_connection_close(conn, connection);
}

void manapi::net::worker::OpenSSL_TLS::_recv_setup_connection(manapi::net::worker::connection &storage) {
    auto &conn_data = storage.as<connection_interface>();
    SSL_set_fd(conn_data.ssl, conn_data.id);
    conn_data.status.fetch_or(CONN_IDLE);

    SSL_set_blocking_mode(conn_data.ssl, 0);
    SSL_set_accept_state(conn_data.ssl);

}

void manapi::net::worker::OpenSSL_TLS::_lookup_event(ev::io &watcher, std::shared_ptr<connection> storage, const int &revents) {
    auto &connection = storage->as<connection_interface>();
    if (connection.status & CONN_CLOSED) {
        this->_ev_watcher_stop (connection);
        return;
    }
    connection.handle(storage, revents);
}

void manapi::net::worker::OpenSSL_TLS::connection_interface_eraser(void *ptr) {
    auto connection = static_cast<connection_interface *> (ptr);
    async::task_run(connection->worker->site.taskpool, [connection] () -> future<void> {
        TCP::_connection_interface_eraser (connection);

        if (connection->ssl) {
            auto lkr = co_await connection->rmx->lock_guard();
            auto lkw = co_await connection->wmx->lock_guard();
            auto ssl = std::exchange(connection->ssl, nullptr);
            MANAPIHTTP_LOG("SSL FREE: {}", connection->id);
            SSL_free(ssl);
            MANAPIHTTP_LOG("SSL CLOSED: {} ssl={:}", connection->id, static_cast<void*>(ssl));
        }
        close(connection->id);
        delete connection;
        co_return;
    } ());
}

int manapi::net::worker::OpenSSL_TLS::_gl_openssl_async_callback(SSL *ssl, void *argp) {
    auto &storage = *static_cast<connection *> (argp);
    auto &conn_data = storage.as<connection_interface>();
    return dynamic_cast<OpenSSL_TLS *> (conn_data.worker)->openssl_async_callback(storage);
}

int manapi::net::worker::OpenSSL_TLS::openssl_async_callback(connection &storage) {
    auto &conn_data = storage.as<connection_interface>();
    return 0;
}

SSL_CTX * manapi::net::worker::OpenSSL_TLS::ssl_create_context(const size_t &version) {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    switch (version)
    {
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        // ReSharper disable once CppDeprecatedEntity
        case http::versions::TLS_v1:      method = TLSv1_server_method();     break;
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        // ReSharper disable once CppDeprecatedEntity
        case http::versions::TLS_v1_1:    method = TLSv1_1_server_method();   break;
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        // ReSharper disable once CppDeprecatedEntity
        case http::versions::TLS_v1_2:    method = TLSv1_2_server_method();   break;
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        case http::versions::TLS_v1_3:    method = TLS_server_method();       break;
        default: THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR,
            "can not find the initialization method openssl (tls_version): {}", version);
    }


    ctx = SSL_CTX_new(method);

    if (!ctx)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot create the openssl context for the tcp connection");
    }

    //SSL_CTX_set_mode(ctx, SSL_MODE_ASYNC);

    //SSL_CTX_set_cipher_list(ctx,"RC4-MD5");
    SSL_CTX_set_alpn_select_cb(ctx, [] (SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
        unsigned int inlen, void *arg) -> int {
        auto worker = static_cast<OpenSSL_TLS *> (arg);
        std::vector <std::string> wishs;
        switch (worker->config->get_http_version()) {
            case http::versions::HTTP_v1_1:
                wishs.emplace_back("http/1.1");
            break;
            case http::versions::HTTP_v2:
                wishs.emplace_back("h2");
            break;
        }

        std::map <std::string_view, int > exists;
        int j = 0;
        for (int i = 0; i < inlen;j++) {
            int plen = in[i++];
            std::string_view buff (reinterpret_cast<const char *>(in) + i, reinterpret_cast<const char *>(in) + i + plen);
            exists.insert({buff, j});
            i += plen;
        }
        for (const auto &wish: wishs) {
            auto it = exists.find(wish);
            if (it != exists.end()) {
                *out = reinterpret_cast<const unsigned char *> (it->first.begin());
                *outlen = it->first.size();
                return 0;
            }
        }
        return -1;
    }, this);

    return ctx;
}

void manapi::net::worker::OpenSSL_TLS::ssl_configure_context() {
    auto sslconfig = config->get_ssl_config();
    if (SSL_CTX_use_certificate_file(ctx, sslconfig->cert.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use cert file openssl");
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, sslconfig->key.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use private key file openssl");
    }
}

std::string manapi::net::worker::OpenSSL_TLS::ssl_get_error(int initerr) {
    std::string result;
    while(initerr!=0)
    {
        result += ERR_error_string(initerr, nullptr);
        result += '\n';
        initerr = ERR_get_error();
    }

    return std::move(result);
}

manapi::net::future<ssize_t> manapi::net::worker::OpenSSL_TLS::ssl_write(connection &conn, const void *buff, const size_t &size) {
    auto &connection = conn.as<connection_interface>();
    auto lk = co_await connection.wmx->lock_guard();
    while (true) {
        int rhs;
        int ssl_errno = SSL_ERROR_NONE;

        if (connection.status & CONN_CLOSED) {
            break;
        }

        rhs = SSL_write(connection.ssl, buff, static_cast<int>(size));
        if (rhs < 0) {
            ssl_errno = SSL_get_error(connection.ssl, static_cast<int>(rhs));

            // if((err = SSL_get_error(SSL*,err)) == SSL_ERROR_ZERO_RETURN)

            if (ssl_errno != SSL_ERROR_NONE) {
                switch (ssl_errno) {
                    case SSL_ERROR_SYSCALL: {
                        int err = errno;
                        co_return -1;
                    }
                    case SSL_ERROR_WANT_READ: {
                        co_await manapi::net::worker::OpenSSL_TLS::io_wait(connection, CONN_READ);
                        continue;
                    }
                    case SSL_ERROR_WANT_WRITE: {
                        co_await manapi::net::worker::OpenSSL_TLS::io_wait(connection, CONN_WRITE);
                        continue;
                    }
                    default:
                        co_return 1;
                }

                break;
            }
        }
        connection.stats.total_write.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}

manapi::net::future<ssize_t> manapi::net::worker::OpenSSL_TLS::ssl_read(connection &conn, void *buff, const size_t &size) {
    auto &connection = conn.as<connection_interface>();

    auto lk = co_await connection.rmx->lock_guard();

    while (true) {
        int rhs;
        int ssl_errno = SSL_ERROR_NONE;

        if (connection.status & CONN_CLOSED) {
            break;
        }

        // if(SSL_get_shutdown(SSL*) & SSL_RECEIVED_SHUTDOWN)
        rhs = SSL_read(connection.ssl, buff, static_cast<int>(size));
        if (rhs < 0) {
            ssl_errno = SSL_get_error(connection.ssl, static_cast<int>(rhs));

            if (ssl_errno != SSL_ERROR_NONE) {
                switch (ssl_errno) {
                    case SSL_ERROR_SYSCALL: {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            co_await manapi::net::worker::OpenSSL_TLS::io_wait(connection, CONN_READ);
                        }
                        continue;
                    }
                    case SSL_ERROR_WANT_READ: {
                        co_await manapi::net::worker::OpenSSL_TLS::io_wait(connection, CONN_READ);
                        continue;
                    }
                    case SSL_ERROR_WANT_WRITE: {
                        co_await manapi::net::worker::OpenSSL_TLS::io_wait(connection, CONN_WRITE);
                        continue;
                    }
                    default:
                        co_return -1;
                }

                break;
            }
        }

        connection.stats.total_read.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}

#endif // MANAPIHTTP_OPENSSL_DEPENDENCY