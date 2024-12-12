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
#include <memory>
#include <netdb.h>
#include <set>
#include <error.h>

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

    if (listen(*fd, 1000) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "LISTEN ERROR. sock_fd: {}", *fd);
    }

    this->write = [this](auto &PH1, auto PH2, auto &PH3, auto PH4) -> future<ssize_t> {
        co_return co_await this->default_write(PH1, PH2, PH3);
    };

    this->read = [this](auto &PH1, auto PH2, auto &PH3) -> future<ssize_t> {
        co_return co_await this->default_read(PH1, PH2, PH3);
    };
}

bool manapi::net::worker::TCP::configure_connection(worker::connection &connection) const {
    auto &conn = connection.as<connection_interface>();

    if (conn.configured) { return true; }
    conn.configured = true;

    return false;
}

manapi::net::future<ssize_t> manapi::net::worker::TCP::response(worker::connection &connection, http_response &resp, bool finish) {
    static const std::string delimiter = "\r\n";
    const auto response = this->stringify_http_info(resp, connection.version, delimiter) + this->stringify_headers(resp, delimiter) + delimiter;

    co_return co_await this->write (connection, response.data(), response.size(), finish);
}

manapi::net::worker::TCP & manapi::net::worker::TCP::operator=(TCP &&n) noexcept {
    base::operator=(std::forward<worker::base>(n));
    this->local = n.local;
    n.local = nullptr;

    return *this;
}

void manapi::net::worker::TCP::onrecv(const std::shared_ptr<worker::base> &worker) {
    auto connection = this->accept();
    site.taskspool->append_task([this, connection = std::move(connection), worker] () -> void {
        if (connection.first) { // new connection
            std::unique_ptr<http::HeaderView> task = std::make_unique<http::HeaderView>(connection.second, worker, config, site);

            if (worker->is_valid_connection(*task->connection)) {
                auto stack = task->doit();
                auto fd = task->connection->as<connection_interface>().id;
                stack._on_connection_finish([this, fd] () -> void {
                    stacks.update([this, &fd] (std::map<int, async_stack_storage> &n) -> void {
                        auto it = n.find(fd);
                        if (it != n.end()) {
                            n.erase(it);
                        }
                        close (fd);
                    });
                });
                stack ();
                if (!stack.finished()) {
                    stacks.update ([&] (auto &n) -> void {
                        n.insert({fd, {
                            .stack = std::move(stack),
                            .storage = std::move(task)
                        }});
                    });
                }
            }
        }
        else { // update connection
            printf("hello world\n %d", connection.second->as<connection_interface>().id);
        }
    });
}

std::shared_ptr<manapi::net::worker::TCP> manapi::net::worker::TCP::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::TCP>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

std::pair<bool, std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::TCP::accept(
    const std::function<std::shared_ptr<connection>()> &init) {
    sockaddr_storage client{};
    socklen_t len = sizeof (client);

    int fd = ::accept(*config->get_socket_fd(), reinterpret_cast<struct sockaddr *>(&client), &len);
    auto stacks_ = stacks.get();
    auto it = stacks_->find(fd);
    if (it == stacks_->end()) {
        auto connection = std::move(init());
        connection->client = client;
        connection->len = len;
        connection->as<connection_interface>().id = fd;
        int flgs = fcntl(fd, F_GETFL, 0);
        flgs |= O_NONBLOCK;
        fcntl(fd, F_SETFL, flgs);
        return {true, std::move(connection)};
    }

    return {false, it->second.storage->connection};
}

std::pair <bool, std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::TCP::accept() {
    return std::move(this->accept([] () { return std::make_shared<worker::connection> (new connection_interface (-1), connection_interface_eraser); }));
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

    // struct timeval timeout{};
    //
    // fd_set fds;
    //
    // FD_ZERO(&fds);
    // FD_SET(fd, &fds);
    //
    // ssize_t tv = static_cast<ssize_t> (flag ? *config->get_send_timeout() : *config->get_recv_timeout());
    // timeout.tv_sec = tv / 1000; // keep alive in n sec
    // timeout.tv_usec = tv - timeout.tv_sec * 1000;
    //
    // int ready;
    //
    // if (flag) { ready = select(fd + 1, nullptr, &fds, nullptr, &timeout); }
    // else { ready = select(fd + 1, &fds, nullptr, nullptr, &timeout); }
    //
    // if (ready < 0) {
    //     MANAPIHTTP_LOG("unknow socket status (select() < 0): {}", fd);
    //     return false;
    // }
    // if (ready == 0) {
    //     MANAPIHTTP_LOG("The waiting time of {} ms has been exceeded",
    //                            *config->get_recv_timeout());
    //     return false;
    // }
    //
    // const bool result = FD_ISSET(fd, &fds);
    //
    // FD_CLR(fd, &fds);

    return true;
}

manapi::net::future<ssize_t> manapi::net::worker::TCP::default_write(connection &conn, const void *buff, const size_t &size) const {
    while (true) {
        ssize_t rhs = ::send(conn.as<connection_interface>().id, buff, size, MSG_NOSIGNAL);
        if (rhs < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        co_return rhs;
    }
    co_return -1;
}

manapi::net::future<ssize_t> manapi::net::worker::TCP::default_read(connection &conn, void *buff, const size_t &size) const {
    while (true) {
        char *buff2 = (char*)buff;
        ssize_t rhs = ::read(conn.as<connection_interface>().id, buff2, size);
        if (rhs < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        co_return rhs;
    }
    co_return -1;
}
