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
#include <future>

#include "ManapiUtils.hpp"
#include "http/HeaderView.hpp"

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

    this->set_fd_non_blocking(*fd);

    if (bind(*fd, local->ai_addr, local->ai_addrlen) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "PORT {} IS ALREADY IN USE", *port);
    }

    if (listen(*fd, 1000) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "LISTEN ERROR. sock_fd: {}", *fd);
    }

    this->write = [this](auto &PH1, auto PH2, auto &PH3, auto PH4) -> future<ssize_t> {
        return this->default_write(PH1, PH2, PH3);
    };

    this->read = [this](auto &PH1, auto PH2, auto &PH3) -> future<ssize_t> {
        return this->default_read(PH1, PH2, PH3);
    };
}

manapi::net::future<bool> manapi::net::worker::TCP::configure_connection(std::shared_ptr<worker::connection> connection) {
    auto &conn = connection->as<connection_interface>();

    if (conn.configured) { co_return true; }
    conn.configured = true;

    co_return false;
}

manapi::net::future<ssize_t> manapi::net::worker::TCP::response(worker::connection &connection, http_response &resp, bool finish) {
    static const std::string delimiter = "\r\n";
    const auto response = this->stringify_http_info(resp, connection.version, delimiter) + this->stringify_headers(resp, delimiter) + delimiter;

    const auto rhs = co_await this->write (connection, response.data(), response.size(), finish);
    co_return rhs;
}

manapi::net::worker::TCP & manapi::net::worker::TCP::operator=(TCP &&n) noexcept {
    base::operator=(std::forward<worker::base>(n));
    this->local = n.local;
    n.local = nullptr;

    return *this;
}

void manapi::net::worker::TCP::disable_watcher_for_status(connection &conn, const connection_status &status) {
    auto &conn_data = conn.as<connection_interface>();
    conn_data.mustly.fetch_xor(status);
}

void manapi::net::worker::TCP::onevent(ev::io &watcher, int revents) {
    if (watcher.data == nullptr || watcher.fd <= 0) {
        MANAPIHTTP_LOG("BUG: ev++.h TCP::onevent(...): watcher.data = nullptr, so that function at 0x0. fd: {}", watcher.fd);
        return;
    }
    std::shared_ptr<connection> connection;
    {
        auto connections_ = this->stacks.get();
        auto it = connections_->find(watcher.fd);
        if (it == connections_->end() || !it->second) {
            return;
        }
        connection = it->second->storage->connection;
    }

    if (!watcher.is_active()) {
        return;
    }

    this->_lookup_event (std::move(connection), revents);
}

void manapi::net::worker::TCP::onrecv(const std::shared_ptr<worker::base> &worker) {
    auto connection = this->accept();
    if (!connection.has_value()) {
        return;
    }

    site.taskpool->append_task([this, connection = std::move(connection.value()), worker] () -> void {
        std::unique_ptr<http::HeaderView> task = std::make_unique<http::HeaderView>(connection, worker, config, site);

        if (worker->is_valid_connection(*task->connection)) {
            auto stack = task->doit ();
            auto &conn = task->connection->as<connection_interface>();
            auto fd = conn.id;

            stack._on_connection_finish([this, connection = std::move(connection), fd] () -> void {
                MANAPIHTTP_LOG("CB FINISHED {}", fd);
                this->connection_close(connection);

                stacks.update([this, &fd] (std::map<int, std::shared_ptr<async_stack_storage>> &n) -> void {
                    auto it = n.find(fd);
                    if (it != n.end()) {
                        auto data = n.extract(it);
                        data.mapped().reset();
                    }
                });
            }, this->site.taskpool);
            std::shared_ptr<async_stack_storage> row;
            stacks.update ([&] (auto &n) -> void {
                auto it = std::make_shared<async_stack_storage>(std::move(stack), std::move(task));
                row = it;
                n[fd] = std::move(it);
            });
            row->stack();
        }
    });
}

std::shared_ptr<manapi::net::worker::TCP> manapi::net::worker::TCP::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::TCP>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::TCP::accept(const std::function<std::shared_ptr<connection>()> &init) {

    if (cnt_conns.load() > 2000) {
        return {};
    }

    sockaddr_storage client{};
    socklen_t len = sizeof (client);
    memset(&client, '\0', sizeof (sockaddr_storage));

    int fd = ::accept(*config->get_socket_fd(), reinterpret_cast<struct sockaddr *>(&client), &len);
    if (fd <= 0) {
        return {};
    }
    bool flag = false;
    this->stacks.update([fd, &flag] (auto &v) -> void {
        if (v.contains(fd)) {
            flag = true;
            return;
        }
        v.insert({fd, nullptr});
    });
    if (flag) {
        return {};
    }

    MANAPIHTTP_LOG("NEW FD: {}", fd);

    cnt_conns.fetch_add(1);
    this->set_fd_non_blocking(fd);

    auto connection = init();
    connection->client = client;
    connection->len = len;
    auto &conn = connection->as<connection_interface>();
    conn.id = fd;
    conn.site = &this->site;
    conn.worker = this;
    conn.timer = site.append_interval(std::chrono::milliseconds(20), [this, connection, &conn] () -> void {
        if (conn.status & CONN_READ & conn.mustly) {
            if (conn.stats.total_read - conn.stats.last_total_read < 8 * 1024) {
                this->connection_close(connection);
            }
        }
        else if (conn.status & CONN_WRITE & conn.mustly) {
            if (conn.stats.total_write - conn.stats.last_total_write < 8 * 1024 ) {
                this->connection_close(connection);
            }
        }

        conn.stats.last_total_read = conn.stats.total_read;
        conn.stats.last_total_write = conn.stats.total_write;
    });

    conn.watcher = std::make_unique<ev::io>(this->loop);
    conn.watcher->set <TCP, &TCP::onevent> (this);
    conn.watcher->start(fd, ev::READ|ev::WRITE);

    return std::move(connection);
}

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::TCP::accept() {
    return std::move(this->accept([this] () {
        auto ms = std::make_shared<worker::connection> (new connection_interface (-1), connection_interface_eraser);
        ms->as<connection_interface>().handle = [this] (auto &&P1, auto &&P2) -> void { this->_io_event (std::forward<decltype(P1)>(P1), std::forward<decltype(P2)>(P2)); };
        return std::move(ms);
    }));
}

void manapi::net::worker::TCP::connection_close(std::shared_ptr<connection> conn) {
    auto &connection = conn->as<connection_interface>();
    std::lock_guard<std::mutex> lk (connection.iomutex);

    if (connection.watcher) {
        connection.watcher->stop();
        connection.watcher.reset();
        shutdown(connection.id, SHUT_RDWR);
    }

    if (connection.timer.has_value()) {
        connection.site->remove_timer(connection.timer.value());
        connection.timer.reset();
    }

    MANAPIHTTP_LOG("connection_close(...) for {}", connection.id);
    connection.status.fetch_or(CONN_CLOSED);

    if (connection.status.load() & CONN_READ) {
        connection.status.fetch_xor(CONN_READ);
        site.taskpool->append_task([conn] () -> void { auto &connection = conn->as<connection_interface>(); connection.iohandle (); });
    }

    if (connection.status.load() & CONN_WRITE) {
        connection.status.fetch_xor(CONN_WRITE);
        site.taskpool->append_task([conn] () -> void { auto &connection = conn->as<connection_interface>(); connection.iohandle (); });
    }
}

void manapi::net::worker::TCP::_lookup_event(std::shared_ptr<connection> storage, const int &revents) {
    auto &connection = storage->as<connection_interface>();
    if (connection.status & CONN_CLOSED) { return; }
    connection.handle(std::move(storage), revents);
}

void manapi::net::worker::TCP::_io_event(std::shared_ptr<connection> storage, int revents) {
    auto &connection = storage->as<connection_interface>();
    auto status = connection.status.load();

    if ((revents & ev::READ) && (status & CONN_READ)) {
        //std::cout << connection.id << " EVENT READ\n";
        connection.status.fetch_xor(CONN_READ);
        site.taskpool->append_task([storage] () -> void {
            auto &connection = storage->as<connection_interface>();
            connection.iohandle ();
        });
        return;
    }

    if ((revents & ev::WRITE) && (status & CONN_WRITE)) {
        connection.status.fetch_xor(CONN_WRITE);
        site.taskpool->append_task([storage] () -> void {
            auto &connection = storage->as<connection_interface>();
            connection.iohandle ();
        });
        return;
    }
}

void manapi::net::worker::TCP::_connection_interface_eraser(connection_interface *connection) {
    shutdown(connection->id, SHUT_RDWR);
    connection->status.fetch_or(CONN_CLOSED);

    if (connection->timer.has_value()) {
        connection->worker->site.remove_timer(connection->timer.value());
        connection->timer.reset();
    }

    if (connection->watcher) {
        connection->watcher->stop();
        connection->watcher.reset();
    }


    connection->worker->cnt_conns.fetch_sub(1);

    close(connection->id);
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
    auto connection = static_cast<connection_interface *> (ptr);
    _connection_interface_eraser(connection);
    delete connection;
}

manapi::net::future<ssize_t> manapi::net::worker::TCP::default_write(connection &conn, const void *buff, const size_t &size) const {
    auto &connection = conn.as<connection_interface>();
    while (true) {
        if (connection.status & CONN_CLOSED) {
            break;
        }

        ssize_t rhs = ::send(connection.id, buff, size, MSG_NOSIGNAL);
        if (rhs < 0) {;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                //std::cout << connection.id << " CB WRITE\n";
                if (connection.status & CONN_CLOSED) {
                    break;
                }
                co_await connection_io_await{connection.iohandle, connection.status, CONN_WRITE};
                continue;
            }
            break;
        }
        connection.stats.total_write.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}

manapi::net::future<ssize_t> manapi::net::worker::TCP::default_read(connection &conn, void *buff, const size_t &size) const {
    auto &connection = conn.as<connection_interface>();
    while (true) {
        if (connection.status & CONN_CLOSED) {
            break;
        }

        ssize_t rhs = ::recv(connection.id, buff, size, MSG_DONTWAIT);
        if (rhs < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                //std::cout << connection.id << " CB READ\n";
                if (connection.status & CONN_CLOSED) {
                    break;
                }
                co_await connection_io_await{connection.iohandle, connection.status, CONN_READ};
                continue;
            }
            break;
        }
        connection.stats.total_read.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}
