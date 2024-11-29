#include "worker/TLS.hpp"

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

#include "ManapiUtils.hpp"

struct connection_interface {
    int id;
    SSL *ssl;
    bool configured = false;
};

manapi::net::worker::TLS::TLS() : base () {
    local = nullptr;
}

manapi::net::worker::TLS::TLS(TLS &&n) noexcept : base (std::forward<worker::base>(n)) {
    this->local = n.local;
    n.local = nullptr;
}

manapi::net::worker::TLS::~TLS() {
    if (local != nullptr) { freeaddrinfo(local); }
}

bool manapi::net::worker::TLS::is_valid_connection(worker::connection &connection) {
    return connection.as<connection_interface>().id >= 0;
}

void manapi::net::worker::TLS::init() {
    hints = {
        .ai_family      = PF_UNSPEC,
        .ai_socktype    = SOCK_STREAM,
        .ai_protocol    = IPPROTO_TCP
    };

    if (getaddrinfo(config->get_address().data(), config->get_port().data(), &hints, &local) != 0) {
        THROW_MANAPI_EXCEPTION(ERR_FATAL, "{}", "failed to resolve host");
    }

    config->set_server_address(*local->ai_addr);
    config->set_server_len(local->ai_addrlen);

    MANAPI_LOG("HTTP TLS PORT USED: {}. {}:{}", config->get_port(), config->get_address(), config->get_port());

    config->set_socket_fd(socket(AF_INET, SOCK_STREAM, 0));
    if (config->get_socket_fd() < 0) {
        THROW_MANAPI_EXCEPTION(ERR_FATAL, "{}", "SOCKET ERROR");
    }
    // REUSE PARAM
    setsockopt(config->get_socket_fd(), SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr_param, sizeof(int));

    // TIMEOUT RECV PARAM
    auto tv = static_cast<ssize_t> (config->get_recv_timeout());
    recv_timeout.tv_sec = tv / 1000;
    recv_timeout.tv_usec = tv - recv_timeout.tv_sec * 1000;;
    setsockopt(config->get_socket_fd(), SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof (timeval));

    // TIMEOUT RECV PARAM
    tv = static_cast<ssize_t> (config->get_send_timeout());
    send_timeout.tv_sec = tv / 1000;
    send_timeout.tv_usec = tv - send_timeout.tv_sec * 1000;
    setsockopt(config->get_socket_fd(), SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof (timeval));

    if (fcntl(config->get_socket_fd(), F_SETFL, O_NONBLOCK) != 0) {
        THROW_MANAPI_EXCEPTION(ERR_FATAL, "Failed to make socket {} non-blocking", config->get_socket_fd());
    }

    if (bind(config->get_socket_fd(), local->ai_addr, local->ai_addrlen) < 0) {
        THROW_MANAPI_EXCEPTION(ERR_FATAL, "PORT {} IS ALREADY IN USE", config->get_port());
    }

    if (listen(config->get_socket_fd(), 10) < 0) {
        THROW_MANAPI_EXCEPTION(ERR_FATAL, "LISTEN ERROR. sock_fd: {}", config->get_socket_fd());
    }

    if (config->get_ssl_config().enabled) {
        // setup ssl certs

        // init
        config->set_openssl_ctx(ssl_create_context(config->get_tls_version()));
        // setup ctx (load certs)
        ssl_configure_context();

        this->write = [this](auto &&PH1, auto &&PH2, auto &&PH3, auto &&PH4) -> ssize_t {
            return ssl_write(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3));
        };

        this->read = [this](auto &&PH1, auto &&PH2, auto &&PH3) -> ssize_t {
            return ssl_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3));
        };
    }
    else {
        this->write = [this](auto &&PH1, auto &&PH2, auto &&PH3, auto &&PH4) -> ssize_t {
            return default_write(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3));
        };

        this->read = [this](auto &&PH1, auto &&PH2, auto &&PH3) -> ssize_t {
            return default_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3));
        };
    }
}

bool manapi::net::worker::TLS::configure_connection(worker::connection &connection) const {
    auto &conn = connection.as<connection_interface>();

    if (conn.configured) { return true; }

    if (established(connection)) {
        auto fd = conn.id;

        if (conn.ssl != nullptr) {
            // SSL setup
            auto ssl = conn.ssl;
            SSL_set_fd(ssl, fd);
            const auto res = SSL_accept(ssl);
            if (!res) {
                THROW_MANAPI_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "couldnt SSL accept: SSL_accept(ssl) = {}", res);
            }
        }

        conn.configured = true;
        return true;
    }

    return false;
}

ssize_t manapi::net::worker::TLS::response(worker::connection &connection, http_response &resp, bool finish) {
    static const std::string delimiter = "\r\n";
    const auto response = this->stringify_http_info(resp, connection.version, delimiter) + this->stringify_headers(resp, delimiter) + delimiter;

    return this->write (connection, response.data(), response.size(), finish);
}

manapi::net::worker::TLS & manapi::net::worker::TLS::operator=(TLS &&n) noexcept {
    base::operator=(std::forward<worker::base>(n));
    this->local = n.local;
    n.local = nullptr;

    return *this;
}

manapi::net::worker::connection manapi::net::worker::TLS::accept() {
    worker::connection connection (new connection_interface (-1, nullptr), connection_interface_eraser);
    connection.as<connection_interface>().id = ::accept(config->get_socket_fd(), reinterpret_cast<struct sockaddr *>(&connection.client), &connection.len);
    if (config->get_ssl_config().enabled) {
        connection.as<connection_interface>().ssl = SSL_new(config->get_openssl_ctx());
    }
    return std::move(connection);
}

std::string manapi::net::worker::TLS::stringify_http_info(manapi::net::http_response &res, const http::versions::http &version, const std::string &delimiter) const {
    return "HTTP/" + http::config::stringify_http_version(version) + ' ' + std::to_string(res.get_status_code()) + (version < http::versions::HTTP_v2 ? ' ' + res.get_status_message() + delimiter : delimiter);
}

std::string manapi::net::worker::TLS::stringify_headers(manapi::net::http_response &res, const std::string &delimiter) const {
    std::string data;
    // add headers
    for (const auto &header: res.get_headers()) {
        data += header.first + ": " + header.second + delimiter;
    }
    return data;
}

void manapi::net::worker::TLS::connection_interface_eraser(void *ptr) {
    if (static_cast<connection_interface *> (ptr)->ssl != nullptr) {
        SSL_shutdown(static_cast<connection_interface *> (ptr)->ssl);
        SSL_free(static_cast<connection_interface *> (ptr)->ssl);
    }
    close(static_cast<connection_interface *> (ptr)->id);
    delete static_cast<connection_interface *> (ptr);
}

SSL_CTX * manapi::net::worker::TLS::ssl_create_context(const size_t &version) {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    switch (version)
    {
        // ReSharper disable once CppDeprecatedEntity
        case http::versions::TLS_v1:      method = TLSv1_server_method();     break;
        // ReSharper disable once CppDeprecatedEntity
        case http::versions::TLS_v1_1:    method = TLSv1_1_server_method();   break;
        // ReSharper disable once CppDeprecatedEntity
        case http::versions::TLS_v1_2:    method = TLSv1_2_server_method();   break;
        case http::versions::TLS_v1_3:    method = TLS_server_method();       break;
        // ReSharper disable once CppDeprecatedEntity
        case http::versions::DTLS_v1:     method = DTLSv1_server_method();    break;
        // ReSharper disable once CppDeprecatedEntity
        case http::versions::DTLS_v1_2:   method = DTLSv1_2_server_method();  break;
        case http::versions::DTLS_v1_3:   method = DTLS_server_method();      break;
        default: THROW_MANAPI_EXCEPTION(ERR_CONFIG_ERROR, "can not find the initialization method openssl (tls_version): {}", version);
    }


    ctx = SSL_CTX_new(method);
    SSL_CTX_set_timeout(ctx, 1);

    if (!ctx)
    {
        THROW_MANAPI_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot create openssl context for tcp connections");
    }

    //SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
    SSL_CTX_set_mode(ctx, SSL_MODE_ASYNC);

    unsigned char vector[] = {
        8, 'h', 't', 't', 'p', '/', '2', '.', '0'
    };

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

void manapi::net::worker::TLS::ssl_configure_context() {
    if (SSL_CTX_use_certificate_file(config->get_openssl_ctx(), config->get_ssl_config().cert.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPI_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use cert file openssl");
    }

    if (SSL_CTX_use_PrivateKey_file(config->get_openssl_ctx(), config->get_ssl_config().key.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPI_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use private key file openssl");
    }
}

bool manapi::net::worker::TLS::established(worker::connection &conn, bool flag) const {
    int fd = conn.as<connection_interface>().id;

    struct timeval timeout{};

    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    ssize_t tv = static_cast<ssize_t> (flag ? config->get_send_timeout() : config->get_recv_timeout());
    timeout.tv_sec = tv / 1000; // keep alive in n sec
    timeout.tv_usec = tv - timeout.tv_sec * 1000;

    int ready;

    if (flag) { ready = select(fd + 1, nullptr, &fds, nullptr, &timeout); }
    else { ready = select(fd + 1, &fds, nullptr, nullptr, &timeout); }

    if (ready < 0) {
        MANAPI_LOG("unknow socket status (select() < 0): {}", fd);
        return false;
    }
    if (ready == 0) {
        MANAPI_LOG("The waiting time of {} ms has been exceeded",
                               config->get_recv_timeout());
        return false;
    }

    const bool result = FD_ISSET(fd, &fds);

    FD_CLR(fd, &fds);

    return result;
}

ssize_t manapi::net::worker::TLS::ssl_write(connection &conn, const void *buff, const size_t &size) const {
    if (!established(conn, true)) {
        return -1;
    }
    return SSL_write (conn.as<connection_interface>().ssl, buff, size);
}

ssize_t manapi::net::worker::TLS::ssl_read(connection &conn, void *buff, const size_t &size) const {
    if (!established(conn)) {
        return -1;
    }
    return SSL_read(conn.as<connection_interface>().ssl, buff, size);
}

ssize_t manapi::net::worker::TLS::default_write(connection &conn, const void *buff, const size_t &size) const {
    if (!established(conn, true)) {
        return -1;
    }
    return ::send(conn.as<connection_interface>().id, buff, size, MSG_NOSIGNAL);
}

ssize_t manapi::net::worker::TLS::default_read(connection &conn, void *buff, const size_t &size) const {
    if (!established(conn)) {
        return -1;
    }

    return ::read(conn.as<connection_interface>().id, buff, size);
}
