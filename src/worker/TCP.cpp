#include "worker/TCP.hpp"

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
#include "http/HeaderView.hpp"

struct connection_interface {
    int id;
    bool configured = false;
};

manapi::net::worker::TCP::TCP(net::site &site) : base (site) {
    local = nullptr;
}

manapi::net::worker::TCP::TCP(TCP &&n) noexcept : base (std::forward<worker::base>(n)) {
    this->local = n.local;
    n.local = nullptr;
}

manapi::net::worker::TCP::~TCP() {
    close (*config->get_socket_fd());
    if (local != nullptr) { freeaddrinfo(local); }
}

bool manapi::net::worker::TCP::is_valid_connection(worker::connection &connection) {
    return connection.as<connection_interface>().id >= 0;
}

void manapi::net::worker::TCP::init() {
    hints = {
        .ai_family      = PF_UNSPEC,
        .ai_socktype    = SOCK_STREAM,
        .ai_protocol    = IPPROTO_TCP
    };

    auto address = config->get_address();
    auto port = config->get_port();

    if (getaddrinfo(address->data(), port->data(), &hints, &local) != 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "failed to resolve host");
    }

    config->set_server_address(*local->ai_addr);
    config->set_server_len(local->ai_addrlen);

    MANAPIHTTP_LOG("HTTP TCP PORT USED: {}. {}:{}", *port, *address, *port);

    config->set_socket_fd(socket(AF_INET, SOCK_STREAM, 0));

    auto fd = config->get_socket_fd();
    if (*fd < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "SOCKET ERROR");
    }
    // REUSE PARAM
    setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr_param, sizeof(int));

    // TIMEOUT RECV PARAM
    auto tv = static_cast<ssize_t> (*config->get_recv_timeout());
    recv_timeout.tv_sec = tv / 1000;
    recv_timeout.tv_usec = tv - recv_timeout.tv_sec * 1000;;
    setsockopt(*fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof (timeval));

    // TIMEOUT RECV PARAM
    tv = static_cast<ssize_t> (*config->get_send_timeout());
    send_timeout.tv_sec = tv / 1000;
    send_timeout.tv_usec = tv - send_timeout.tv_sec * 1000;
    setsockopt(*fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof (timeval));

    if (fcntl(*fd, F_SETFL, O_NONBLOCK) != 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "Failed to make socket {} non-blocking", *fd);
    }

    if (bind(*fd, local->ai_addr, local->ai_addrlen) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "PORT {} IS ALREADY IN USE", *port);
    }

    if (listen(*fd, 10) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "LISTEN ERROR. sock_fd: {}", *fd);
    }

    this->write = [this](auto &&PH1, auto &&PH2, auto &&PH3, auto &&PH4) -> ssize_t {
        return this->default_write(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3));
    };

    this->read = [this](auto &&PH1, auto &&PH2, auto &&PH3) -> ssize_t {
        return this->default_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3));
    };
}

bool manapi::net::worker::TCP::configure_connection(worker::connection &connection) const {
    auto &conn = connection.as<connection_interface>();

    if (conn.configured) { return true; }

    conn.configured = true;

    return false;
}

ssize_t manapi::net::worker::TCP::response(worker::connection &connection, http_response &resp, bool finish) {
    static const std::string delimiter = "\r\n";
    const auto response = this->stringify_http_info(resp, connection.version, delimiter) + this->stringify_headers(resp, delimiter) + delimiter;

    return this->write (connection, response.data(), response.size(), finish);
}

manapi::net::worker::TCP & manapi::net::worker::TCP::operator=(TCP &&n) noexcept {
    base::operator=(std::forward<worker::base>(n));
    this->local = n.local;
    n.local = nullptr;

    return *this;
}

void manapi::net::worker::TCP::onrecv(const std::shared_ptr<worker::base> &worker) {
    std::unique_ptr<http::HeaderView> task = std::make_unique<http::HeaderView>(worker, config, site);

    if (worker->is_valid_connection(*task->connection)) {
        site.append_task(std::move(task), 1);
        std::this_thread::yield();
    }
}

std::shared_ptr<manapi::net::worker::TCP> manapi::net::worker::TCP::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::TCP>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

manapi::net::worker::connection manapi::net::worker::TCP::accept() {
    worker::connection connection (new connection_interface (-1), connection_interface_eraser);
    connection.as<connection_interface>().id = ::accept(*config->get_socket_fd(), reinterpret_cast<struct sockaddr *>(&connection.client), &connection.len);
    return std::move(connection);
}

std::string manapi::net::worker::TCP::stringify_http_info(manapi::net::http_response &res, const http::versions::http &version, const std::string &delimiter) const {
    return "HTTP/" + http::config::stringify_http_version(version) + ' ' + std::to_string(res.get_status_code()) + (version < http::versions::HTTP_v2 ? ' ' + res.get_status_message() + delimiter : delimiter);
}

std::string manapi::net::worker::TCP::stringify_headers(manapi::net::http_response &res, const std::string &delimiter) const {
    std::string data;
    // add headers
    for (const auto &header: res.get_headers()) {
        data += header.first + ": " + header.second + delimiter;
    }
    return data;
}

void manapi::net::worker::TCP::connection_interface_eraser(void *ptr) {
    close(static_cast<connection_interface *> (ptr)->id);
    delete static_cast<connection_interface *> (ptr);
}

bool manapi::net::worker::TCP::established(worker::connection &conn, bool flag) const {
    int fd = conn.as<connection_interface>().id;

    struct timeval timeout{};

    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    ssize_t tv = static_cast<ssize_t> (flag ? *config->get_send_timeout() : *config->get_recv_timeout());
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

ssize_t manapi::net::worker::TCP::default_write(connection &conn, const void *buff, const size_t &size) const {
    if (!established(conn, true)) {
        return -1;
    }
    return ::send(conn.as<connection_interface>().id, buff, size, MSG_NOSIGNAL);
}

ssize_t manapi::net::worker::TCP::default_read(connection &conn, void *buff, const size_t &size) const {
    if (!established(conn, false)) {
        return -1;
    }

    return ::read(conn.as<connection_interface>().id, buff, size);
}
