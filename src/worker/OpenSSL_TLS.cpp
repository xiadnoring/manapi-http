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
        ctx = ssl_create_context(*config->get_tls_version());
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

    conn.mustly.fetch_xor(CONN_READ | CONN_WRITE);
    std::shared_ptr<size_t> timerid = std::make_unique<size_t>(0);
    *timerid = this->site.append_timer(std::chrono::milliseconds(500), [timerid, this, conn = &conn, connection] () -> void {
        *timerid = 0;
        MANAPIHTTP_LOG("TIMEOUT SSL_ACCEPT: {}", conn->id);
        this->connection_close(std::move(connection));
    });

    while (true) {
        if (conn.status & CONN_CLOSED) {
            this->site.remove_timer(*timerid);
            co_return false;
        }

        MANAPIHTTP_LOG("SSL ACCEPT: {}", conn.id);

        const auto rhs = SSL_accept(conn.ssl);
        auto ssl_error = SSL_get_error(conn.ssl, rhs);
        if (ssl_error != SSL_ERROR_NONE) {
            switch (ssl_error) {
                case SSL_ERROR_WANT_READ:
                    co_await connection_io_await{conn.iohandle, conn.status, conn.iomutex, CONN_READ};
                    continue;
                break;
                case SSL_ERROR_WANT_WRITE:
                    co_await connection_io_await{conn.iohandle, conn.status, conn.iomutex, CONN_WRITE};
                    continue;
                break;
                case SSL_ERROR_SSL:
                    MANAPIHTTP_LOG("SSL_ERROR_SSL: fd: {}", conn.id);
                    this->connection_close(std::move(connection));
                    co_return false;
                case SSL_ERROR_SYSCALL:
                    MANAPIHTTP_LOG("SSL_ERROR_SYSCALL: {}, fd: {}", errno, conn.id);
                    this->connection_close(std::move(connection));
                    co_return false;
                default:
                    MANAPIHTTP_LOG("SSL_accept(...) = {}, ssl_error = {}", rhs, ssl_error);
                    this->connection_close(std::move(connection));
                    co_return false;
            }
        }

        break;
    }

    this->site.remove_timer(*timerid);
    conn.mustly.fetch_or(CONN_READ | CONN_WRITE);
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

void manapi::net::worker::OpenSSL_TLS::onrecv(ev::io &watcher, int revents) {
    auto connection_optional = this->accept();
    if (!connection_optional.has_value()) {
        return;
    }

    auto &connection = connection_optional.value();

    auto worker = std::shared_ptr<net::worker::base>(this->worker);
    std::shared_ptr <http::HeaderView> task = std::make_shared<http::HeaderView>(connection, worker, config, site);

    if (worker->is_valid_connection(*task->connection)) {
        auto stack = std::make_shared<future<>>(task->doit ());
        auto &conn_data = task->connection->as<connection_interface>();
        auto fd = conn_data.id;
        SSL_set_fd(conn_data.ssl, fd);
        conn_data.status.fetch_or(CONN_IDLE);
        //SSL_set_mode(conn_data.ssl, SSL_MODE_ASYNC);
        //SSL_set_blocking_mode(conn_data.ssl, 0);
        SSL_set_accept_state(conn_data.ssl);

        std::shared_ptr<async_stack_storage> row = std::make_shared<async_stack_storage>(stack, std::move(task));

        stack->_on_connection_finish([this, connection, row, fd] () -> void {
            MANAPIHTTP_LOG("CB FINISHED {}", fd);
            this->connection_close(connection);
            row->stack.reset();
        }, this->site.taskpool);

        stacks[fd] = row;

        site.taskpool->append_task([this, connection, row] () -> void {
            row->stack->operator()();
        });
    }
}

std::shared_ptr<manapi::net::worker::OpenSSL_TLS> manapi::net::worker::OpenSSL_TLS::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::OpenSSL_TLS>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::OpenSSL_TLS::accept() {
    auto connection = TCP::accept([this] () {
        auto ms = std::make_shared<worker::connection> (new connection_interface {}, connection_interface_eraser);
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

void manapi::net::worker::OpenSSL_TLS::connection_close(std::shared_ptr<connection> conn) {
    auto &connection = conn->as<connection_interface>();
    std::lock_guard<std::mutex> lk (connection.iomutex);
    // int ssl_errno = SSL_get_error(connection.ssl, SSL_shutdown(connection.ssl));
    // if (ssl_errno != SSL_ERROR_NONE && ssl_errno != SSL_ERROR_ZERO_RETURN) {
    //     MANAPIHTTP_LOG("NO WAY: {}", ssl_errno);
    // }
    this->_connection_close(conn, connection);
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
    SSL_CTX_set_cipher_list(ctx,"RC4-MD5");

    SSL_CTX_set_alpn_select_cb(ctx, [] (SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
        unsigned int inlen, void *arg) -> int {
        static const std::vector <std::string> wishs = {"h3", "h2", "http/1.1"};

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
                return it->second;
            }
        }
        return -1;
    }, nullptr);

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
    while (true) {
        int rhs;
        int ssl_errno = SSL_ERROR_NONE;

        {
            auto lock = co_await connection.wmx->lock_guard();
            if (connection.status & CONN_CLOSED) {
                break;
            }
            rhs = SSL_write(connection.ssl, buff, static_cast<int>(size));
            if (rhs < 0) {
                ssl_errno = SSL_get_error(connection.ssl, static_cast<int>(rhs));
            }
        }

        // if((err = SSL_get_error(SSL*,err)) == SSL_ERROR_ZERO_RETURN)

        if (ssl_errno != SSL_ERROR_NONE) {
            switch (ssl_errno) {
                case SSL_ERROR_SYSCALL: {
                    int err = errno;
                    co_return -1;
                }
                case SSL_ERROR_WANT_WRITE:
                    co_await connection_io_await{connection.iohandle, connection.status, connection.iomutex, CONN_WRITE};
                    continue;
                break;
                case SSL_ERROR_WANT_READ:
                    co_await connection_io_await{connection.iohandle, connection.status, connection.iomutex, CONN_READ};
                break;
                case SSL_ERROR_WANT_ASYNC:
                    continue;
                break;
                case SSL_ERROR_SSL:
                    co_return -1;
            }

            break;
        }
        if (rhs < 0) {
            break;
        }

        connection.stats.total_write.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}

manapi::net::future<ssize_t> manapi::net::worker::OpenSSL_TLS::ssl_read(connection &conn, void *buff, const size_t &size) {
    auto &connection = conn.as<connection_interface>();
    while (true) {
        int rhs;
        int ssl_errno = SSL_ERROR_NONE;
        {
            auto lock = co_await connection.rmx->lock_guard();
            if (connection.status & CONN_CLOSED) {
                break;
            }
            // if(SSL_get_shutdown(SSL*) & SSL_RECEIVED_SHUTDOWN)
            rhs = SSL_read(connection.ssl, buff, static_cast<int>(size));
            if (rhs < 0) {
                ssl_errno = SSL_get_error(connection.ssl, static_cast<int>(rhs));
            }
        }
        if (ssl_errno != SSL_ERROR_NONE) {
            switch (ssl_errno) {
                case SSL_ERROR_SYSCALL: {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        co_await connection_io_await{connection.iohandle, connection.status, connection.iomutex, CONN_READ};
                        continue;
                    }
                    int err = errno;
                    co_return -1;
                }
                case SSL_ERROR_WANT_READ:
                    co_await connection_io_await{connection.iohandle, connection.status, connection.iomutex, CONN_READ};
                    continue;
                break;
                case SSL_ERROR_WANT_WRITE:
                    co_await connection_io_await{connection.iohandle, connection.status, connection.iomutex, CONN_WRITE};
                break;
                case SSL_ERROR_WANT_ASYNC:
                    continue;
                break;
                case SSL_ERROR_SSL:
                    co_return -1;
            }

            break;
        }
        if (rhs < 0) {
            continue;
        }
        connection.stats.total_read.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}

#endif // MANAPIHTTP_OPENSSL_DEPENDENCY