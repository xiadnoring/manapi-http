#include "worker/TCP.hpp"
#include "ManapiParams.hpp"

#include <iostream>
#include <csignal>
#include <utility>
#include <vector>
#include <memory.h>
#if defined(__unix__)||defined(__APPLE__)
#   include <arpa/inet.h>
#   include <netinet/tcp.h>
#   include <netdb.h>
#   include <error.h>
#endif
#if defined(_WIN32)
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <winsock2.h>
#   include <ws2tcpip.h>
#endif
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <memory>
#include <set>
#include <future>

#include "ManapiUtils.hpp"
#include "async/ManapiAsyncSocket.hpp"
#include "http/HeaderView.hpp"

#if defined(_WIN32)
#   pragma comment(lib, "Ws2_32.lib")
#endif

#define IPV6_REGEX R"((([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])))"
#define IPV4_REGEX R"(((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))"

manapi::net::worker::TCP::TCP(net::site &site) : base (site) {
    this->local = nullptr;
}

manapi::net::worker::TCP::~TCP() {
#if defined(_WIN32)
    ::closesocket(config->get_socket_fd());
#else
    ::close (config->get_socket_fd());
#endif
    if (local != nullptr) { freeaddrinfo(local); }
}

bool manapi::net::worker::TCP::is_valid_connection(worker::connection &connection) {
    return connection.as<connection_interface>().id >= 0;
}

int get_ip_version (std::string_view ip) {
    std::regex ipv4 {IPV4_REGEX};
    std::regex ipv6 {IPV6_REGEX};

    if (std::regex_match(ip.data(), ipv4)) {
        return 4;
    }
    if (std::regex_match(ip.data(), ipv6)) {
        return 6;
    }

    return 0;
}

void manapi::net::worker::TCP::init() {
    this->hints = {
        .ai_family      = PF_UNSPEC,
        .ai_socktype    = SOCK_STREAM,
        .ai_protocol    = IPPROTO_TCP
    };

    auto address = *this->config->get_address();
    auto port = *this->config->get_port();
    const int version = get_ip_version(address);

    if (!version) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "Invalid IP address: {}", address);
    }

    if (getaddrinfo(address.data(), port.data(), &hints, &local) != 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "failed to resolve host");
    }

    this->config->set_server_address(*this->local->ai_addr);
    this->config->set_server_len(this->local->ai_addrlen);

    MANAPIHTTP_LOG("HTTP TCP PORT USED: {}. {}:{}", port, address, port);

    this->config->set_socket_fd(socket(this->local->ai_family, SOCK_STREAM, 0));

    auto &fd = config->get_socket_fd();
    if (fd < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "SOCKET ERROR");
    }
    // REUSE PARAM
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &socket_param_true, sizeof(this->socket_param_true));

    if (this->config->get_tcp_no_delay()) {
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &socket_param_true, sizeof (this->socket_param_true));
    }

    manapi::async::set_non_blocking(fd);

    if (bind(fd.load(), local->ai_addr, local->ai_addrlen) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "PORT {} IS ALREADY IN USE", port);
    }

    if (listen(fd, this->config->max_backlog()) < 0) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "LISTEN ERROR. sock_fd: {}", fd.load());
    }

    this->write = [this](auto &PH1, auto PH2, auto PH3, auto PH4) -> future<ssize_t> {
        return this->default_write(PH1, PH2, PH3);
    };

    this->read = [this](auto &PH1, auto PH2, auto PH3) -> future<ssize_t> {
        return this->default_read(PH1, PH2, PH3);
    };

    /* every 1 second */
    this->limit_rate_timer = this->site.async_context()->timerpool()->append_interval_sync(1000,
        [this] (manapi::timer t) -> void { this->update_limit_rate(); });
    this->limit_rate_cv = std::make_shared<async::condition_variable>(this->site.async_context());
}

manapi::future<bool> manapi::net::worker::TCP::configure_connection(std::shared_ptr<worker::connection> connection) {
    auto &conn = connection->as<connection_interface>();

    if (conn.configured) { co_return true; }
    conn.configured = true;

    co_return true;
}

manapi::future<ssize_t> manapi::net::worker::TCP::response(worker::connection &connection, http::response &resp, bool finish) {
    static const std::string delimiter = "\r\n";
    const auto response = this->stringify_http_info(resp, connection.version, delimiter) + this->stringify_headers(resp, delimiter) + delimiter;

    const auto rhs = co_await this->write (connection, response.data(), response.size(), finish);
    co_return rhs;
}

void manapi::net::worker::TCP::onrecv(ev::io &watcher, int revents) {
    if (this->config->max_connections() <= this->stacks.size()) {
        watcher.priority = priority::lowcapacity;
        return;
    }

    if (watcher.priority != priority::onaccept) {
        watcher.priority = priority::onaccept;
    }

    auto connection_optional = this->accept();
    if (!connection_optional.has_value()) {
        return;
    }

    auto connection = std::move(connection_optional.value());
    auto worker = std::shared_ptr<worker::base>(this->worker);
    std::shared_ptr <http::HeaderView> task = std::make_shared<http::HeaderView>(connection, worker, config, site);

    if (worker->is_valid_connection(*task->connection)) {
        auto stack = std::make_shared<future<>>(task->doit ());
        auto &conn = task->connection->as<connection_interface>();
        auto fd = conn.id;
        this->recv_setup_connection (*connection);
        std::shared_ptr<async_stack_storage> row = std::make_shared<async_stack_storage>(stack, task);

        stack->on_finish([this, connection, fd, row] () mutable -> void {
            auto r = std::move(row);
            auto conn = std::move(connection);
            async::run(this->site.async_context(), this->site.async_context()->eventloop()->custom_callback([this, conn = std::move(conn)] (event_loop *ev) mutable
                    -> void {
                    auto &connection = conn->as<connection_interface>();
                    connection.t.sync_stop(this->site.async_context());
                    this->_ev_watcher_stop(connection);
                    this->cnt_conns.fetch_sub(1);

                    this->connection_close(conn, true);
                }));
        }, this->site.async_context()->taskpool());

        this->stacks[fd] = row;

        this->site.async_context()->taskpool()->append_task([this, row = std::move(row)] ()
            -> void { row->stack->operator()(); });
    }
}

std::shared_ptr<manapi::net::worker::TCP> manapi::net::worker::TCP::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::TCP>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::TCP::accept(const std::function<std::shared_ptr<connection>()> &init) {
    sockaddr_storage client{};
    socklen_t len = sizeof (client);
    memset(&client, '\0', sizeof (sockaddr_storage));

    int fd = ::accept(this->config->get_socket_fd(), reinterpret_cast<struct sockaddr *>(&client), &len);
    if (fd < 0) {
        return {};
    }

    auto it = this->stacks.insert({fd, nullptr});
    if (!it.second) {
        return {};
    }

    //MANAPIHTTP_LOG("NEW FD: {}", fd);

    this->cnt_conns.fetch_add(1);
    manapi::async::set_non_blocking(fd);

    auto connection = init();
    connection->client = client;
    connection->len = len;
    auto &conn = connection->as<connection_interface>();
    conn.id = fd;
    conn.site = &this->site;
    conn.worker = std::shared_ptr (this->worker);
    conn.t = this->site.async_context()->timerpool()->append_interval_sync(this->config->speed_check_delay(),
        [weak_connection = std::weak_ptr (connection)] (manapi::timer t) mutable -> void {
            auto connection = weak_connection.lock();
            connection->as<connection_interface>().worker->_timeout(std::move(connection));
        });

    return std::move(connection);
}

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::TCP::accept() {
    return std::move(this->accept([this] () {
        auto ms = std::make_shared<worker::connection> (new connection_interface (), connection_interface_eraser);
        auto &conn_data = ms->as<connection_interface>();
        return std::move(ms);
    }));
}

void manapi::net::worker::TCP::connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) {
    auto &connection = conn->as<connection_interface>();

    if (connection.iocancel) {
        connection.iocancel.sync_cancel();
        connection.iocancel = nullptr;
    }

    if (connection.t) {
        connection.t.sync_stop(this->site.async_context());
        connection.t = nullptr;
    }

    this->_connection_close(conn, connection);
}

void manapi::net::worker::TCP::stop() {
    /* in the libev main loop */
    this->limit_rate_timer.sync_stop(this->site.async_context());
}

int manapi::net::worker::TCP::status(connection &conn) {
    return conn.as<connection_io_await>().iostatus;
}

void manapi::net::worker::TCP::
connection_shutdown(std::shared_ptr<connection> conn, bool connection_status) {
    auto &connection = conn->as<connection_interface>();
    int flags = 0;
    if (connection_status & CONN_READ) { flags = SHUT_RD; }
    if (connection_status & CONN_WRITE) {
        if (connection_status & CONN_READ) { flags = SHUT_RDWR; }
        else { flags = SHUT_WR; }
    }
    ::shutdown(connection.id, flags);
}

void manapi::net::worker::TCP::connection_cancel(std::shared_ptr<connection> conn) {
    auto &connection = conn->as<connection_interface>();

    if (connection.iocancel) {
        connection.iocancel.sync_cancel();
        connection.iocancel = nullptr;
    }
}

ssize_t manapi::net::worker::TCP::sync_read(worker::connection *conn, void *buff, ssize_t size) {
    auto &connection = conn->as<connection_interface>();

    auto rhs = std::min(this->config->speed_limit_rate().load() - connection.stats.transfared_last_second.load(), size);
    if (!rhs && size) {
        if (connection.watcher) { connection.watcher->stop(); }
        connection.status.fetch_or(CONN_LIMIT_RATE);
    }

    connection.stats.transfared_last_second.fetch_add(rhs);

    rhs = ::read(connection.id, buff, size);
    connection.stats.total_read.fetch_add(rhs);
    return rhs;
}

ssize_t manapi::net::worker::TCP::sync_write(worker::connection *conn, const void *buff, ssize_t size) {
    auto &connection = conn->as<connection_interface>();

    auto rhs = std::min(this->config->speed_limit_rate().load() - connection.stats.transfared_last_second.load(), size);
    if (!rhs && size) {
        if (connection.watcher) { connection.watcher->stop(); }
        connection.status.fetch_or(CONN_LIMIT_RATE);
    }
    connection.stats.transfared_last_second.fetch_add(rhs);

    rhs = ::write(connection.id, buff, size);
    connection.stats.total_write.fetch_add(rhs);
    return rhs;
}

manapi::future<std::shared_ptr<ev::io>> manapi::net::worker::TCP::async_watch_io(worker::connection *conn, int revents,
    std::move_only_function<void(ev::io &w, int revents)> callback) {
    auto &connection = conn->as<connection_interface>();
    if (!connection.watcher) {
        connection.watcher = co_await this->site.async_context()->eventloop()->watch_fd(conn->as<connection_interface>().id, revents, std::move(callback));
    }
    co_return connection.watcher;
}

std::shared_ptr<ev::io> manapi::net::worker::TCP::sync_watch_io(worker::connection *conn, int revents,
    std::move_only_function<void(ev::io &w, int revents)> callback) {
    auto &connection = conn->as<connection_interface>();
    if (!connection.watcher) {
        connection.watcher = this->site.async_context()->eventloop()->create_watcher_fd(connection.id, revents, std::move(callback));
        connection.watcher->start();
    }
    return connection.watcher;
}


void manapi::net::worker::TCP::recv_setup_connection(manapi::net::worker::connection &storage) {}

void manapi::net::worker::TCP::update_limit_rate() {
    /* in the event loop */
    for (auto &conn: this->stacks) {
        this->update_limit_rate_connection(*conn.second->storage->connection);
    }
    async::run(this->site.async_context(), this->limit_rate_cv->notify_all());
}

void manapi::net::worker::TCP::_timeout(std::shared_ptr<connection> storage) {
    auto &conn = storage->as<connection_interface>();

    bool flag = false;
    auto status = conn.status.load();

    if (status & CONN_CLOSED) {
        flag = true;
    }
    else if (status & CONN_READ) {
        if (conn.stats.total_read - conn.stats.last_total_read < this->config->speed_check_bytes()) {
            flag = true;
        }
    }
    else if (status & CONN_WRITE) {
        if (conn.stats.total_write - conn.stats.last_total_write < this->config->speed_check_bytes()) {
            flag = true;
        }
    }

    if (flag) {
        //MANAPIHTTP_LOG("TIMEOUT {} t:{}", conn.id, storage.use_count());
        conn.t.sync_stop(this->site.async_context());
        conn.status.fetch_or(CONN_CLOSED);
        if (conn.iocancel) {
            conn.iocancel.sync_cancel();
            conn.iocancel = nullptr;
        }
        // async::run(this->site.async_context(), this->connection_close(storage, false));
        return;
    }


    conn.stats.last_total_read = conn.stats.total_read;
    conn.stats.last_total_write = conn.stats.total_write;
}

void manapi::net::worker::TCP::_ev_watcher_stop(connection_interface &conn) {
    this->stacks.erase(conn.id);
}

void manapi::net::worker::TCP::_connection_close(std::shared_ptr<connection> conn, connection_interface &connection) {

}

void manapi::net::worker::TCP::update_limit_rate_connection(connection &conn) {
    auto &conn_data = conn.as<connection_interface>();
    conn_data.stats.transfared_last_second.store(0);
    const auto flag = conn_data.status.load();
    if (conn_data.watcher && (!flag & CONN_CLOSED)
        && !conn_data.watcher->is_active() && conn_data.watcher->data) { conn_data.watcher->start(); }
    if (flag & CONN_LIMIT_RATE) { conn_data.status.fetch_xor(CONN_LIMIT_RATE); }
}

void manapi::net::worker::TCP::_connection_interface_eraser(connection_interface *connection) {

}

std::string manapi::net::worker::TCP::stringify_http_info(manapi::net::http::response &res, const int &version, const std::string &delimiter) const {
    return "HTTP/" + http::config::stringify_http_version(version) + ' ' + std::to_string(res.status_code()) + (version < http::versions::HTTP_v2 ? ' ' + std::string{res.status_message()} + delimiter : delimiter);
}

std::string manapi::net::worker::TCP::stringify_headers(manapi::net::http::response &res, const std::string &delimiter) const {
    std::string data;
    // add headers
    for (const auto &header: res.ref_headers()) {
        data += header.first + ": " + header.second + delimiter;
    }
    return data;
}

void manapi::net::worker::TCP::connection_interface_eraser(void *ptr) {
    auto connection = static_cast<connection_interface *> (ptr);
    //MANAPIHTTP_LOG("close {}", connection->id);
    _connection_interface_eraser(connection);
#if defined(_WIN32)
    ::closesocket(connection->id);
#else
    ::close(connection->id);
#endif
    //MANAPIHTTP_LOG("CLOSE(...) {}", connection->id);
    delete connection;
}

manapi::future<ssize_t> manapi::net::worker::TCP::default_write(connection &conn, const void *buff, ssize_t size) const {
    auto &connection = conn.as<connection_interface>();
    while (true) {
        if (connection.status & CONN_CLOSED) {
            break;
        }

        const auto limit_rate = this->config->speed_limit_rate().load();
        if (connection.stats.transfared_last_second >= limit_rate) {
            connection.status.fetch_or(CONN_LIMIT_RATE);
            co_await this->limit_rate_cv->wait([&] ()
                -> bool { return connection.status & CONN_CLOSED || connection.stats.transfared_last_second.load() < limit_rate; });

            if (connection.status & CONN_CLOSED) {
                break;
            }
        }

        size = std::min(limit_rate - connection.stats.transfared_last_second.load(), size);

        int flg = 0;
        #if defined(__unix__)||defined(__APPLE__)
            flg |= MSG_DONTWAIT|MSG_NOSIGNAL;
        #endif
#ifdef _WIN32
        ssize_t rhs = ::send(connection.id, static_cast<const char *>(buff), size, flg);
#else
        ssize_t rhs = ::send(connection.id, buff, size, flg);
#endif
        if (rhs < 0) {
            connection.status.fetch_or(CONN_WRITE);
            connection.iocancel.reset(this->site.async_context());
            connection.iocancel.ask_cancel_callback();
            co_await async::write_ready (this->site.async_context(), connection.id, connection.iocancel);
            connection.status.fetch_xor(CONN_WRITE);

            continue;
        }
        connection.stats.total_write.fetch_add(rhs);
        connection.stats.transfared_last_second.fetch_add(size);
        co_return rhs;
    }
    co_return -1;
}

manapi::future<ssize_t> manapi::net::worker::TCP::default_read(connection &conn, void *buff, ssize_t size) const {
    auto &connection = conn.as<connection_interface>();

    while (true) {
        if (connection.status & CONN_CLOSED) {
            break;
        }

        const auto limit_rate = this->config->speed_limit_rate().load();
        if (connection.stats.transfared_last_second >= limit_rate) {
            connection.status.fetch_or(CONN_LIMIT_RATE);
            co_await this->limit_rate_cv->wait([&] ()
                -> bool { return connection.status & CONN_CLOSED || connection.stats.transfared_last_second < limit_rate; });

            if (connection.status & CONN_CLOSED) {
                break;
            }
        }

        size = std::min(limit_rate - connection.stats.transfared_last_second.load(), size);


        int flg = 0;
        #if defined(__unix__)||defined(__APPLE__)
            flg |= MSG_DONTWAIT;
        #endif
#ifdef _WIN32
        ssize_t rhs = ::recv(connection.id, static_cast<char*>(buff), size, flg);
#else
        ssize_t rhs = ::recv(connection.id, buff, size, flg);
#endif
        if (rhs < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                connection.status.fetch_or(CONN_READ);
                connection.iocancel.reset(this->site.async_context());
                connection.iocancel.ask_cancel_callback();
                co_await async::read_ready (this->site.async_context(), connection.id, connection.iocancel);
                connection.status.fetch_xor(CONN_READ);
                continue;
            }
            break;
        }
        connection.stats.transfared_last_second.fetch_add(size);
        connection.stats.total_read.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}
