#include "worker/OpenSSL_TLS.hpp"

#include "async/ManapiAsyncSocket.hpp"
#include "ManapiInitTools.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY

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

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "ManapiUtils.hpp"
#include "http/HeaderView.hpp"

#define WANT_READ(x_, ctx) x_.status.fetch_or(CONN_READ); \
x_.iocancel.reset(ctx);\
x_.iocancel.handle_ready([&] () -> void { x_.iomutex.unlock(); unlock.disable(); lk.call(); }); \
co_await async::read_ready (this->site.async_context(), x_.id, x_.iocancel); \
x_.status.fetch_xor(CONN_READ);
#define WANT_WRITE(x_, ctx) x_.status.fetch_or(CONN_WRITE); \
x_.iocancel.reset(ctx);\
x_.iocancel.handle_ready([&] () -> void { x_.iomutex.unlock(); unlock.disable(); lk.call();  }); \
co_await async::write_ready (this->site.async_context(), x_.id, x_.iocancel); \
x_.status.fetch_xor(CONN_WRITE);

manapi::net::worker::OpenSSL_TLS::OpenSSL_TLS(net::site &site) : TCP (site) {
    init_tools::ssl_library_init();
}

manapi::net::worker::OpenSSL_TLS::~OpenSSL_TLS() {
    SSL_CTX_free(this->ctx);
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

        this->write = [this](auto &PH1, auto PH2, auto PH3, auto PH4)
            -> future<ssize_t> { return ssl_write(PH1, PH2, PH3); };

        this->read = [this](auto &PH1, auto PH2, auto PH3)
            -> future<ssize_t> { return ssl_read(PH1, PH2, PH3); };
    }
}

manapi::future<bool> manapi::net::worker::OpenSSL_TLS::configure_connection(std::shared_ptr<connection> connection) {
    auto &conn = connection->as<connection_interface>();

    if (conn.configured) {
        co_return true;
    }

    if (!SSL_is_init_finished(conn.ssl)) {
        conn.mustly.fetch_xor(CONN_READ | CONN_WRITE);

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

            int rhs = SSL_accept(conn.ssl);
            rhs = SSL_get_error(conn.ssl, rhs);

            if (rhs != SSL_ERROR_NONE) {
                switch (rhs) {
                    case SSL_ERROR_WANT_READ: {
                        WANT_READ(conn, this->site.async_context());
                        goto cont;
                    }
                    case SSL_ERROR_WANT_WRITE: {
                        WANT_WRITE(conn, this->site.async_context());
                        goto cont;
                    }
                    case SSL_ERROR_ZERO_RETURN: {
                        conn.iomutex.unlock();
                        unlock.disable();

                        co_await conn.accept_timer.async_stop(this->site.async_context());
                        co_await this->connection_close(std::move(connection), true);
                        co_return false;
                    }
                    case SSL_ERROR_SSL:
                    case SSL_ERROR_SYSCALL:
                    default:
                    error: {
                        conn.iomutex.unlock();
                        unlock.disable();

                        co_await conn.accept_timer.async_stop(this->site.async_context());
                        co_await this->connection_close(std::move(connection), false);
                        co_return false;
                    }
                }
cont:
                lk = co_await conn.mx->lock_guard();
                if (!conn.iomutex.try_to_lock()) {
                    co_return false;
                }

                unlock.enable();
            }

            if (SSL_is_init_finished(conn.ssl)) {
                break;
            }
        }

        co_await conn.accept_timer.async_stop(this->site.async_context());
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
        auto ms = std::make_shared<worker::connection> (new connection_interface {this->site.async_context()}, connection_interface_eraser);
        auto &connection = ms->as<connection_interface>();
        connection.ssl = this->config->get_ssl_config()->enabled ? SSL_new(this->ctx) : nullptr;
        connection.mx = std::make_unique<async::mutex>(this->site.async_context());
        connection.accept_timer = this->site.async_context()->timerpool()->append_timer_sync(8000, [this, ms] (manapi::timer t)
            -> void {
            async::run(this->site.async_context(), this->connection_close(ms, false));
        });
        return std::move(ms);
    });

    return std::move(connection);
}

manapi::future<void> manapi::net::worker::OpenSSL_TLS::connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) {
    if (!conn) {
        co_return;
    }


    auto &connection = conn->as<connection_interface>();
    auto lk = co_await connection.iomutex.lock_guard();


    if (connection.iocancel) {
        co_await connection.iocancel.cancel();
        connection.iocancel = nullptr;
    }

    if ((false && false == connection.status & CONN_CLOSED)) {
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
                auto rhs = SSL_shutdown(connection.ssl);
                int ssl_errno = SSL_get_error(connection.ssl, rhs);
                if (ssl_errno == SSL_ERROR_NONE) {
                    break;
                }
                switch (ssl_errno) {
                    case SSL_ERROR_WANT_READ:
                        co_await async::read_ready(this->site.async_context(), connection.id, connection.iocancel);
                    break;
                    case SSL_ERROR_WANT_WRITE:
                        co_await async::write_ready(this->site.async_context(), connection.id, connection.iocancel);
                    break;
                    case SSL_ERROR_SYSCALL: {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            co_await async::read_ready(this->site.async_context(), connection.id, connection.iocancel);
                            break;
                        }
                        flag=false;
                        break;
                    }
                    default:
                        flag = false;
                    break;
                }
            }
            while (flag);

            co_await timer.async_stop(this->site.async_context()->eventloop());
        }
        else {
            SSL_set_shutdown(connection.ssl, SSL_SENT_SHUTDOWN|SSL_RECEIVED_SHUTDOWN);
        }
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

void manapi::net::worker::OpenSSL_TLS::update_limit_rate_connection(connection &conn) {
    auto &conn_data = conn.as<connection_interface>();
    conn_data.stats.transfared_last_second.store(0);
    async::run(this->site.async_context(), this->limit_rate_cv->notify_all());
}


void manapi::net::worker::OpenSSL_TLS::connection_interface_eraser(void *ptr) {
    auto connection = static_cast<connection_interface *> (ptr);
    async::run(connection->worker->site.async_context(), [connection] () mutable -> future<void> {
        TCP::_connection_interface_eraser (connection);
        auto prev = connection;
        if (connection->ssl) {
            auto ssl = std::exchange(connection->ssl, nullptr);
            //MANAPIHTTP_LOG("SSL FREE: {}", connection->id);
            SSL_free(ssl);
        }
            //MANAPIHTTP_LOG("SSL CLOSED: {}", connection->id);
#ifdef _WIN32
        ::closesocket(connection->id);
#else
        ::close(connection->id);
#endif
        delete connection;
        co_return;
    });
}

int manapi::net::worker::OpenSSL_TLS::_gl_openssl_async_callback(SSL *ssl, void *argp) {
    auto &storage = *static_cast<connection *> (argp);
    auto &conn_data = storage.as<connection_interface>();
    return dynamic_cast<OpenSSL_TLS*> (conn_data.worker.get())->openssl_async_callback(storage);
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

    // SSL_CTX_set_max_send_fragment(ctx, this->config->buffer_size());
    // SSL_CTX_set_default_read_buffer_len(ctx, this->config->buffer_size());

    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2|SSL_OP_NO_TICKET);
    SSL_CTX_set_session_id_context(ctx, reinterpret_cast<const unsigned char *>(&this->ssl_session_ctx_id), sizeof(this->ssl_session_ctx_id));

    //SSL_CTX_set_cipher_list(ctx,"TLS_AES_256_GCM_SHA384");
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
                *out = reinterpret_cast<const unsigned char *> (it->first.data());
                *outlen = it->first.size();
                return 0;
            }
        }
        return -1;
    }, this);

    return ctx;
}

void manapi::net::worker::OpenSSL_TLS::ssl_configure_context() {
    auto sslconfig = this->config->get_ssl_config();
    if (SSL_CTX_use_certificate_file(this->ctx, sslconfig->cert.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use cert file openssl");
    }

    if (SSL_CTX_use_PrivateKey_file(this->ctx, sslconfig->key.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use private key file openssl");
    }

    if (!SSL_CTX_check_private_key(this->ctx)) {
        MANAPIHTTP_LOG("Private key does not match the certificate public key.\nCertificate File: {}, Pivate Key File: {}", sslconfig->cert.data(), sslconfig->key.data());
    }

    SSL_CTX_set_verify(this->ctx, this->config->get_verify_peer().load() ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_verify_depth(this->ctx, 1);
}

void manapi::net::worker::OpenSSL_TLS::ssl_get_error() {
    size_t initerr = ERR_get_error();
    std::string result;
    while(initerr!=0)
    {
        result += ERR_error_string(initerr, nullptr);
        result += '\n';
        initerr = ERR_get_error();
    }
    if (!result.empty()) {
        MANAPIHTTP_LOG("OpenSSL ERROR: {}", result);
    }
}

int manapi::net::worker::OpenSSL_TLS::status(connection &conn) {
    return conn.as<connection_interface>().status;
}

manapi::future<ssize_t> manapi::net::worker::OpenSSL_TLS::ssl_write(connection &conn, const void *buff, ssize_t size) {
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
        int ssl_errno = SSL_ERROR_NONE;

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
            connection.status.fetch_xor(CONN_LIMIT_RATE);
            if (!connection.iomutex.try_to_lock()) {
                co_return -1;
            }

            unlock.enable();
        }

        size = std::min(limit_rate - connection.stats.transfared_last_second.load(), size);
        connection.stats.transfared_last_second.fetch_add(size);

        rhs = SSL_write(connection.ssl, buff, static_cast<int>(size));
        ssl_errno = SSL_get_error(connection.ssl, static_cast<int>(rhs));

        if (rhs < 0) {
            // if((err = SSL_get_error(SSL*,err)) == SSL_ERROR_ZERO_RETURN)


            if (ssl_errno != SSL_ERROR_NONE) {
                switch (ssl_errno) {
                    case SSL_ERROR_SYSCALL: {
                        int err = errno;
                        co_return -1;
                    }
                    case SSL_ERROR_WANT_READ: {
                        WANT_READ(connection, this->site.async_context());
                        goto cont;
                    }
                    case SSL_ERROR_WANT_WRITE: {
                        WANT_WRITE(connection, this->site.async_context());
                        goto cont;
                    }
                    default:
                        co_return -1;
                }

                break;
cont:
                lk = co_await connection.mx->lock_guard();
                if (!connection.iomutex.try_to_lock()) {
                    co_return -1;
                }

                unlock.enable();
                continue;
            }
        }
        connection.stats.total_write.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}

manapi::future<ssize_t> manapi::net::worker::OpenSSL_TLS::ssl_read(connection &conn, void *buff, ssize_t size) {
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
        int ssl_errno = SSL_ERROR_NONE;

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
            connection.status.fetch_xor(CONN_LIMIT_RATE);

            if (!connection.iomutex.try_to_lock()) {
                co_return -1;
            }

            unlock.enable();
        }

        size = std::min(limit_rate - connection.stats.transfared_last_second.load(), size);
        connection.stats.transfared_last_second.fetch_add(size);

        // if(SSL_get_shutdown(SSL*) & SSL_RECEIVED_SHUTDOWN)
        rhs = SSL_read(connection.ssl, buff, static_cast<int>(size));
        ssl_errno = SSL_get_error(connection.ssl, static_cast<int>(rhs));

        if (rhs < 0) {

            if (ssl_errno != SSL_ERROR_NONE) {
                switch (ssl_errno) {
                    case SSL_ERROR_SYSCALL: {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            WANT_READ(connection, this->site.async_context());
                            goto cont;
                        }
                        co_return -1;
                    }
                    case SSL_ERROR_WANT_READ: {
                        WANT_READ(connection, this->site.async_context());
                        goto cont;
                    }
                    case SSL_ERROR_WANT_WRITE: {
                        WANT_WRITE(connection, this->site.async_context());
                        goto cont;
                    }
                    default:
                        co_return -1;
                }

                break;
cont:
                lk = co_await connection.mx->lock_guard();

                if (!connection.iomutex.try_to_lock()) {
                    co_return -1;
                }

                unlock.enable();
                continue;
            }
        }

        connection.stats.total_read.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}

#endif // MANAPIHTTP_OPENSSL_DEPENDENCY