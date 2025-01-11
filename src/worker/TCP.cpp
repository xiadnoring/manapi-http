#include "worker/TCP.hpp"

#include <iostream>
#include <csignal>
#include <utility>
#include <vector>
#include <memory.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
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
    close (config->get_socket_fd());
    if (local != nullptr) { freeaddrinfo(local); }
}

bool manapi::net::worker::TCP::is_valid_connection(worker::connection &connection) {
    return connection.as<connection_interface>().id >= 0;
}

void manapi::net::worker::TCP::init() {
    this->hints = {
        .ai_family      = PF_UNSPEC,
        .ai_socktype    = SOCK_STREAM,
        .ai_protocol    = IPPROTO_TCP
    };

    auto address = config->get_address();
    auto port = config->get_port();

    if (getaddrinfo(address->data(), port->data(), &hints, &local) != 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "failed to resolve host");
    }

    this->config->set_server_address(*local->ai_addr);
    this->config->set_server_len(local->ai_addrlen);

    MANAPIHTTP_LOG("HTTP TCP PORT USED: {}. {}:{}", *port, *address, *port);

    this->config->set_socket_fd(socket(AF_INET, SOCK_STREAM, 0));

    auto &fd = config->get_socket_fd();
    if (fd < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "SOCKET ERROR");
    }
    // REUSE PARAM
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &socket_param_true, sizeof(int));

    // TIMEOUT RECV PARAM
    auto tv = static_cast<ssize_t> (config->get_recv_timeout());
    this->recv_timeout.tv_sec = tv / 1000;
    this->recv_timeout.tv_usec = tv - recv_timeout.tv_sec * 1000;;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof (timeval));

    // TIMEOUT RECV PARAM
    tv = static_cast<ssize_t> (config->get_send_timeout());
    this->send_timeout.tv_sec = tv / 1000;
    this->send_timeout.tv_usec = tv - send_timeout.tv_sec * 1000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof (timeval));

    if (this->config->get_tcp_no_delay()) {
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &socket_param_true, sizeof (int));
    }

    this->set_fd_non_blocking(fd);

    if (bind(fd.load(), local->ai_addr, local->ai_addrlen) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "PORT {} IS ALREADY IN USE", *port);
    }

    if (listen(fd, 2000) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "LISTEN ERROR. sock_fd: {}", fd.load());
    }

    this->write = [this](auto &PH1, auto PH2, auto &PH3, auto PH4) -> future<ssize_t> {
        return this->default_write(PH1, PH2, PH3);
    };

    this->read = [this](auto &PH1, auto PH2, auto &PH3) -> future<ssize_t> {
        return this->default_read(PH1, PH2, PH3);
    };
}

manapi::future<bool> manapi::net::worker::TCP::configure_connection(std::shared_ptr<worker::connection> connection) {
    auto &conn = connection->as<connection_interface>();

    if (conn.configured) { co_return true; }
    conn.configured = true;

    co_return true;
}

manapi::future<ssize_t> manapi::net::worker::TCP::response(worker::connection &connection, http_response &resp, bool finish) {
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
    if (watcher.data == nullptr || watcher.fd < 0) {
        MANAPIHTTP_LOG("BUG: ev++.h TCP::onevent(...): watcher.data = nullptr, so that function at 0x0. fd: {}", watcher.fd);
        return;
    }

    std::shared_ptr<connection> connection;
    {
        auto &connections_ = this->stacks;
        auto it = connections_.find(watcher.fd);

        if (it == connections_.end()) {
            return;
        }

        if (!it->second) {
            return;
        }

        connection = it->second->storage->connection;
    }

    this->_lookup_event (watcher, connection, revents);
}

void manapi::net::worker::TCP::onrecv(ev::io &watcher, int revents) {
    auto connection_optional = this->accept();
    if (!connection_optional.has_value()) {
        return;
    }

    auto &connection = connection_optional.value();
    auto worker = std::shared_ptr<worker::base>(this->worker);
    std::shared_ptr <http::HeaderView> task = std::make_shared<http::HeaderView>(connection, worker, config, site);

    if (worker->is_valid_connection(*task->connection)) {
        auto stack = std::make_shared<future<>>(task->doit ());
        auto &conn = task->connection->as<connection_interface>();
        auto fd = conn.id;
        this->_recv_setup_connection (*connection);
        std::shared_ptr<async_stack_storage> row = std::make_shared<async_stack_storage>(stack, task);

        stack->on_finish([this, connection, fd, row] () mutable -> void {
            async::run(this->site.taskpool, [this, fd, connection] () mutable  -> future<void> {
                co_await this->connection_close(connection, true);
            });
            row->stack.reset();
        }, this->site.taskpool);

        this->stacks[fd] = row;

        this->site.taskpool->append_task([this, connection, row] () -> void {
            row->stack->operator()();
        });
    }
}

std::shared_ptr<manapi::net::worker::TCP> manapi::net::worker::TCP::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::TCP>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::TCP::accept(const std::function<std::shared_ptr<connection>()> &init) {
    // if (cnt_conns.load() > 2000) {
    //     return {};
    // }

    sockaddr_storage client{};
    socklen_t len = sizeof (client);
    memset(&client, '\0', sizeof (sockaddr_storage));

    int fd = ::accept(config->get_socket_fd(), reinterpret_cast<struct sockaddr *>(&client), &len);
    if (fd < 0) {
        return {};
    }
    bool flag = false;

    auto it = this->stacks.insert({fd, nullptr});
    if (!it.second) {
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
    conn.timer.data = new decltype(connection) (connection);

    ev_timer_init(&conn.timer, _ev_timeout, 0.2, 0.);
    ev_timer_start(this->loop, &conn.timer);

    conn.watcher = std::make_shared<ev::io>(this->loop);
    conn.watcher->priority = 2;
    conn.watcher->set <TCP, &TCP::onevent> (this);
    conn.watcher->start(fd, ev::READ|ev::WRITE);

    return std::move(connection);
}

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::TCP::accept() {
    return std::move(this->accept([this] () {
        auto ms = std::make_shared<worker::connection> (new connection_interface (this->site.taskpool), connection_interface_eraser);
        ms->as<connection_interface>().handle = [this] (auto &&P1, auto &&P2) -> void { this->_io_event (std::forward<decltype(P1)>(P1), std::forward<decltype(P2)>(P2)); };
        return std::move(ms);
    }));
}

manapi::future<void> manapi::net::worker::TCP::connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) {
    auto &connection = conn->as<connection_interface>();
    auto lk = co_await connection.iomutex.lock_guard();
    this->_connection_close(conn, connection);
}

void manapi::net::worker::TCP::_recv_setup_connection(manapi::net::worker::connection &storage) {}

manapi::future<> manapi::net::worker::TCP::io_wait(connection_interface &connection, const int &status) {
    co_await connection.iomutex.lock();
    co_await connection_io_await{connection.iohandle, connection.status, connection.iomutex, status};
}

void manapi::net::worker::TCP::_timeout(std::shared_ptr<connection> storage, const int &revents) {
    auto &conn = storage->as<connection_interface>();

    bool flag = false;
    auto status = conn.status.load();
    auto mustly = conn.mustly.load();

    if (status & CONN_CLOSED) {
        flag = true;
    }
    else if (status & CONN_READ & mustly) {
        if (conn.stats.total_read - conn.stats.last_total_read < 16 * 1024) {
            flag = true;
        }
    }
    else if (status & CONN_WRITE & mustly) {
        if (conn.stats.total_write - conn.stats.last_total_write < 16 * 1024) {
            flag = true;
        }
    }

    conn.timer.repeat = 0.2; // 200ms
    ev_timer_again(this->loop, &conn.timer);

    if (flag) {
        this->_ev_watcher_stop(conn);
        async::run(this->site.taskpool, this->connection_close(storage, false));
        return;
    }


    conn.stats.last_total_read = conn.stats.total_read;
    conn.stats.last_total_write = conn.stats.total_write;
}

void manapi::net::worker::TCP::_ev_watcher_stop(connection_interface &conn) {
    if (ev_is_active(&conn.timer)) {
        ev_timer_stop(this->loop, &conn.timer);
        delete static_cast<std::shared_ptr<connection> *> (std::exchange(conn.timer.data, nullptr));
        conn.watcher->stop();
        //MANAPIHTTP_LOG("WATCHER STOP: {}", conn.id);
        this->stacks.erase(conn.id);
    }
}

void manapi::net::worker::TCP::_ev_timeout(struct ev_loop *loop, ev_timer *w, int revents) {
    auto storage = *static_cast<std::shared_ptr<connection> *>( w->data);
    storage->as<connection_interface>().worker->_timeout(storage, revents);
}

void manapi::net::worker::TCP::_connection_close(std::shared_ptr<connection> conn, connection_interface &connection) {
    if ((connection.status & CONN_CLOSED) == false) {
        connection.status.fetch_or(CONN_CLOSED);
    }

    //MANAPIHTTP_LOG("connection_close(...) for {}", connection.id);

    if (connection.status.load() & CONN_READ) {
    //MANAPIHTTP_LOG("CONN_READ(...) for {}", connection.id);
        connection.status.fetch_xor(CONN_READ);
        site.taskpool->append_task([conn, &connection] () -> void { connection.iohandle (); });
    }

    if (connection.status.load() & CONN_WRITE) {
    //MANAPIHTTP_LOG("CONN_WRITE(...) for {}", connection.id);
        connection.status.fetch_xor(CONN_WRITE);
        site.taskpool->append_task([conn, &connection] () -> void { connection.iohandle (); });
    }
}

void manapi::net::worker::TCP::_lookup_event(ev::io &watcher, std::shared_ptr<connection> storage, const int &revents) {
    auto &connection = storage->as<connection_interface>();
    if (connection.status & CONN_CLOSED) {
        this->_ev_watcher_stop (connection);
        return;
    }
    connection.handle(storage, revents);
}

void manapi::net::worker::TCP::_io_event(std::shared_ptr<connection> storage, int revents) {
    auto &connection = storage->as<connection_interface>();
    auto status = connection.status.load();

    //::cerr << connection.id << " " << revents << "\n";

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
    connection->status.fetch_or(CONN_CLOSED);
    while (connection->watcher->is_active() || connection->watcher->is_pending()) {
        MANAPIHTTP_LOG("watcher is steel active fd: ", connection->id);
    }

    connection->worker->cnt_conns.fetch_sub(1);
}

std::string manapi::net::worker::TCP::stringify_http_info(manapi::net::http_response &res, const http::versions::http &version, const std::string &delimiter) const {
    return "HTTP/" + http::config::stringify_http_version(version) + ' ' + std::to_string(res.get_status_code()) + (version < http::versions::HTTP_v2 ? ' ' + res.get_status_message() + delimiter : delimiter);
}

std::string manapi::net::worker::TCP::stringify_headers(manapi::net::http_response &res, const std::string &delimiter) const {
    std::string data;
    // add headers
    for (const auto &header: res.ref_headers()) {
        data += header.first + ": " + header.second + delimiter;
    }
    return data;
}

void manapi::net::worker::TCP::connection_interface_eraser(void *ptr) {
    auto connection = static_cast<connection_interface *> (ptr);
    _connection_interface_eraser(connection);
    ::close(connection->id);
    MANAPIHTTP_LOG("CLOSE(...) {}", connection->id);
    delete connection;
}

manapi::future<ssize_t> manapi::net::worker::TCP::default_write(connection &conn, const void *buff, const size_t &size) const {
    auto &connection = conn.as<connection_interface>();
    while (true) {
        if (connection.status & CONN_CLOSED) {
            break;
        }

        ssize_t rhs = ::send(connection.id, buff, size, MSG_NOSIGNAL|MSG_DONTWAIT);
        if (rhs < 0) {
            if (connection.status & CONN_CLOSED) {
                break;
            }
            co_await io_wait(connection, CONN_WRITE);
            continue;
        }
        connection.stats.total_write.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}

manapi::future<ssize_t> manapi::net::worker::TCP::default_read(connection &conn, void *buff, const size_t &size) const {
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
                co_await io_wait(connection, CONN_READ);
                continue;
            }
            break;
        }
        connection.stats.total_read.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}
