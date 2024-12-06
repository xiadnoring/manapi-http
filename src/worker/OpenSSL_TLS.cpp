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

struct connection_interface {
    int id;
    SSL *ssl;
    bool configured = false;
};

manapi::net::Atomic <bool> manapi::net::worker::OpenSSL_TLS::gl_init = false;

void ssl_library_init (bool &gl_init) {
    if (gl_init == false) {
        SSL_library_init();
        SSL_load_error_strings();
        gl_init = true;
    }
}

manapi::net::worker::OpenSSL_TLS::OpenSSL_TLS(net::site &site) : TCP (site) {
    gl_init.update(ssl_library_init);
}

manapi::net::worker::OpenSSL_TLS::OpenSSL_TLS(OpenSSL_TLS &&n) noexcept : TCP (std::forward<worker::TCP>(n)) {}

manapi::net::worker::OpenSSL_TLS::~OpenSSL_TLS() {
    if (ctx != nullptr) { SSL_CTX_free(ctx); }
}

bool manapi::net::worker::OpenSSL_TLS::is_valid_connection(worker::connection &connection) {
    return connection.as<connection_interface>().id >= 0;
}

void manapi::net::worker::OpenSSL_TLS::init() {
    TCP::init();
    auto sslconfig = config->get_ssl_config();
    if (sslconfig->enabled) {
        // setup ssl certs

        // init
        ctx = ssl_create_context(*config->get_tls_version());
        // setup ctx (load certs)
        ssl_configure_context();

        this->write = [this](auto &&PH1, auto &&PH2, auto &&PH3, auto &&PH4) -> ssize_t {
            return ssl_write(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3));
        };

        this->read = [this](auto &&PH1, auto &&PH2, auto &&PH3) -> ssize_t {
            return ssl_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3));
        };
    }
}

bool manapi::net::worker::OpenSSL_TLS::configure_connection(worker::connection &connection) const {
    auto &conn = connection.as<connection_interface>();

    if (conn.configured) { return true; }

    if (established(connection, false)) {
        auto fd = conn.id;

        if (conn.ssl != nullptr) {
            // SSL setup
            auto ssl = conn.ssl;
            SSL_set_fd(ssl, fd);
            const auto rhs = SSL_accept(ssl);
            if (!rhs) {
                THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "couldnt SSL accept: SSL_accept(ssl) = {}", rhs);
            }
        }

        conn.configured = true;
        return true;
    }

    return false;
}

manapi::net::worker::OpenSSL_TLS & manapi::net::worker::OpenSSL_TLS::operator=(OpenSSL_TLS &&n) noexcept {
    TCP::operator=(std::forward<worker::TCP>(n));
    return *this;
}

void manapi::net::worker::OpenSSL_TLS::onrecv(const std::shared_ptr<worker::base> &worker) {
    std::unique_ptr<http::HeaderView> task = std::make_unique<http::HeaderView>(worker, config, site);

    if (worker->is_valid_connection(*task->connection)) {
        site.append_task(std::move(task), 1);
        std::this_thread::yield();
    }
}

std::shared_ptr<manapi::net::worker::OpenSSL_TLS> manapi::net::worker::OpenSSL_TLS::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::OpenSSL_TLS>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

manapi::net::worker::connection manapi::net::worker::OpenSSL_TLS::accept() {
    worker::connection connection (new connection_interface (-1, nullptr), connection_interface_eraser);
    connection.as<connection_interface>().id = ::accept(*config->get_socket_fd(), reinterpret_cast<struct sockaddr *>(&connection.client), &connection.len);
    if (config->get_ssl_config()->enabled) {
        connection.as<connection_interface>().ssl = SSL_new(ctx);
    }
    return std::move(connection);
}

void manapi::net::worker::OpenSSL_TLS::connection_interface_eraser(void *ptr) {
    if (static_cast<connection_interface *> (ptr)->ssl != nullptr) {
        SSL_shutdown(static_cast<connection_interface *> (ptr)->ssl);
        SSL_free(static_cast<connection_interface *> (ptr)->ssl);
    }
    close(static_cast<connection_interface *> (ptr)->id);
    delete static_cast<connection_interface *> (ptr);
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
    SSL_CTX_set_timeout(ctx, 1);

    if (!ctx)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot create the openssl context for the tcp connection");
    }

    SSL_CTX_set_mode(ctx, SSL_MODE_ASYNC & SSL_MODE_AUTO_RETRY);

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

std::string manapi::net::worker::OpenSSL_TLS::ssl_get_error() {
    std::string result;
    unsigned long l_err = ERR_get_error();
    while(l_err!=0)
    {
        result += ERR_error_string(l_err, nullptr);
        result += '\n';
        l_err = ERR_get_error();
    }

    return std::move(result);
}

bool manapi::net::worker::OpenSSL_TLS::established(worker::connection &conn, bool flag) const {
    int fd = conn.as<connection_interface>().id;

    struct timeval timeout{};

    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    ssize_t tv = static_cast<ssize_t> (flag ? (*config->get_send_timeout()) : (*config->get_recv_timeout()));
    timeout.tv_sec = tv / 1000; // keep alive in n sec
    timeout.tv_usec = tv - timeout.tv_sec * 1000;

    int ready;

    if (flag) { ready = select(fd + 1, nullptr, &fds, nullptr, &timeout); }
    else { ready = select(fd + 1, &fds, nullptr, nullptr, &timeout); }

    if (ready < 0) {
        MANAPIHTTP_LOG("unknow socket status (select() < 0): {}", fd);
        return false;
    }
    if (ready == 0) {
        MANAPIHTTP_LOG("The waiting time of {} ms has been exceeded",
                               *config->get_recv_timeout());
        return false;
    }

    const bool result = FD_ISSET(fd, &fds);

    FD_CLR(fd, &fds);

    return result;
}

ssize_t manapi::net::worker::OpenSSL_TLS::ssl_write(connection &conn, const void *buff, const size_t &size) {
    std::lock_guard<std::mutex> lk (wmx);
    if (!established(conn, true)) {
        return -1;
    }
    return SSL_write (conn.as<connection_interface>().ssl, buff, size);
}

ssize_t manapi::net::worker::OpenSSL_TLS::ssl_read(connection &conn, void *buff, const size_t &size) {
    std::lock_guard<std::mutex> lk (rmx);
    if (!established(conn, false)) {
        return -1;
    }
    return SSL_read (conn.as<connection_interface>().ssl, buff, size);
}

#endif // MANAPIHTTP_OPENSSL_DEPENDENCY