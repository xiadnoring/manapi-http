#include "worker/TCP.hpp"

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
    local = nullptr;
}

manapi::net::worker::TCP::TCP(TCP &&n) noexcept : base (std::forward<worker::base>(n)) {
    this->local = n.local;
    n.local = nullptr;
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

    // TIMEOUT RECV PARAM
    auto tv = static_cast<ssize_t> (config->get_recv_timeout());
    this->recv_timeout.tv_sec = tv / 1000;
    this->recv_timeout.tv_usec = tv - recv_timeout.tv_sec * 1000;;
#if _WIN32
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recv_timeout), sizeof (timeval));
#else
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof (timeval));
#endif

    // TIMEOUT RECV PARAM
    tv = static_cast<ssize_t> (config->get_send_timeout());
    this->send_timeout.tv_sec = tv / 1000;
    this->send_timeout.tv_usec = tv - send_timeout.tv_sec * 1000;
#ifdef _WIN32
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&send_timeout), sizeof (timeval));
#else
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof (timeval));
#endif

    if (this->config->get_tcp_no_delay()) {
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &socket_param_true, sizeof (this->socket_param_true));
    }

    this->set_fd_non_blocking(fd);

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

void manapi::net::worker::TCP::onevent(ev::io &watcher, int revents, std::shared_ptr<connection> conn) {
    if (conn) {
        this->_lookup_event (watcher, std::move(conn), revents);
    }
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
        this->_recv_setup_connection (*connection);
        std::shared_ptr<async_stack_storage> row = std::make_shared<async_stack_storage>(stack, task);

        stack->on_finish([this, connection, fd, row] () mutable -> void {
            async::run(this->site.async_context(), [this, fd, connection] () mutable  -> future<void> {
                co_await this->connection_close(connection, true);
            });
            row->stack.reset();
        }, this->site.async_context()->taskpool());

        this->stacks[fd] = row;

        this->site.async_context()->taskpool()->append_task([this, connection, row = std::move(row)] () -> void {
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
    sockaddr_storage client{};
    socklen_t len = sizeof (client);
    memset(&client, '\0', sizeof (sockaddr_storage));

    int fd = ::accept(this->config->get_socket_fd(), reinterpret_cast<struct sockaddr *>(&client), &len);
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
    conn.worker = std::shared_ptr (this->worker);
    conn.timer.data = new decltype(connection) (connection);

    ev_timer_init(&conn.timer, _ev_timeout, static_cast<double>(this->config->speed_check_delay()) / 1000, 0.);
    ev_timer_start(this->le->get_loop(), &conn.timer);

    return std::move(connection);
}

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::TCP::accept() {
    return std::move(this->accept([this] () {
        auto ms = std::make_shared<worker::connection> (new connection_interface (this->site.async_context()), connection_interface_eraser);
        auto &conn_data = ms->as<connection_interface>();
        conn_data.handle = [this] (auto &&P0, auto &&P1, auto &&P2) -> void { this->_io_event (std::forward<decltype(P0)>(P0), std::forward<decltype(P1)>(P1), std::forward<decltype(P2)>(P2)); };

        return std::move(ms);
    }));
}

manapi::future<void> manapi::net::worker::TCP::connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) {
    auto &connection = conn->as<connection_interface>();
    auto lk = co_await connection.iomutex.lock_guard();
    this->_connection_close(conn, connection);
    co_await this->limit_rate_cv->notify_all();
}

void manapi::net::worker::TCP::stop() {
    /* in the libev main loop */
    this->limit_rate_timer.sync_stop(this->site.async_context());
}

int manapi::net::worker::TCP::status(connection &conn) {
    return conn.as<connection_io_await>().iostatus;
}

void manapi::net::worker::TCP::_recv_setup_connection(manapi::net::worker::connection &storage) {}

void manapi::net::worker::TCP::update_limit_rate() {
    /* in the event loop */
    for (auto &conn: this->stacks) {
        this->update_limit_rate_connection(*conn.second->storage->connection);
    }
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
        if (conn.stats.total_read - conn.stats.last_total_read < this->config->speed_check_bytes()) {
            flag = true;
        }
    }
    else if (status & CONN_WRITE & mustly) {
        if (conn.stats.total_write - conn.stats.last_total_write < this->config->speed_check_bytes()) {
            flag = true;
        }
    }

    /* divided by 1 second */
    conn.timer.repeat = static_cast<double>(this->config->speed_check_delay()) / 1000;
    ev_timer_again(this->le->get_loop(), &conn.timer);

    if (flag) {
        this->_ev_watcher_stop(conn);
        async::run(this->site.async_context(), this->connection_close(storage, false));
        return;
    }


    conn.stats.last_total_read = conn.stats.total_read;
    conn.stats.last_total_write = conn.stats.total_write;
}

void manapi::net::worker::TCP::_ev_watcher_stop(connection_interface &conn) {
    if (ev_is_active(&conn.timer)) {
        ev_timer_stop(this->le->get_loop(), &conn.timer);
        delete static_cast<std::shared_ptr<connection> *> (std::exchange(conn.timer.data, nullptr));
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

    if (connection.wcancel) {
        async::run(this->site.async_context(), async::invoke(connection.wcancel));
    }

    if (connection.rcancel) {
        async::run(this->site.async_context(), async::invoke(connection.rcancel));
    }
}

void manapi::net::worker::TCP::update_limit_rate_connection(connection &conn) {
    auto &conn_data = conn.as<connection_interface>();
    conn_data.stats.transfared_last_second.store(0);

    async::run(this->site.async_context(), this->limit_rate_cv->notify_all());
}

void manapi::net::worker::TCP::_lookup_event(ev::io &watcher, std::shared_ptr<connection> storage, const int &revents) {
    auto &connection = storage->as<connection_interface>();
    if (connection.status & CONN_CLOSED) {
        this->_ev_watcher_stop (connection);
        return;
    }
    connection.handle(watcher, storage, revents);
}

void manapi::net::worker::TCP::_io_event(ev::io &w, std::shared_ptr<connection> storage, int revents) {

}

void manapi::net::worker::TCP::_connection_interface_eraser(connection_interface *connection) {
    connection->status.fetch_or(CONN_CLOSED);
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

    if (!connection.iomutex.try_to_lock()) {
        co_return -1;
    }

    auto unlock = before_delete([&] ()
        -> void { connection.iomutex.unlock(); });

    while (true) {
        if (connection.status & CONN_CLOSED) {
            break;
        }

        const auto limit_rate = this->config->speed_limit_rate().load();
        if (connection.stats.transfared_last_second >= limit_rate) {
            unlock.disable();
            connection.iomutex.unlock();

            connection.status.fetch_or(CONN_LIMIT_RATE);
            co_await this->limit_rate_cv->wait([&] ()
                -> bool { return connection.status & CONN_CLOSED || connection.stats.transfared_last_second.load() < limit_rate; });
            connection.status.fetch_xor(CONN_LIMIT_RATE);

            if (!connection.iomutex.try_to_lock()) {
                co_return -1;
            }

            unlock.enable();
        }

        size = std::min(limit_rate - connection.stats.transfared_last_second.load(), size);
        connection.stats.transfared_last_second.fetch_add(size);

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
            unlock.disable();

            connection.status.fetch_or(CONN_WRITE);
            co_await async::write_ready (this->site.async_context(), connection.id, &connection.wcancel, [&] ()
                -> void { connection.iomutex.unlock(); unlock.disable(); });
            connection.status.fetch_xor(CONN_WRITE);

            if (!connection.iomutex.try_to_lock()) {
                co_return -1;
            }
            unlock.enable();
            continue;
        }
        connection.stats.total_write.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}

manapi::future<ssize_t> manapi::net::worker::TCP::default_read(connection &conn, void *buff, ssize_t size) const {
    auto &connection = conn.as<connection_interface>();

    if (!connection.iomutex.try_to_lock()) {
        co_return -1;
    }

    auto unlock = before_delete([&] ()
        -> void { connection.iomutex.unlock(); });

    while (true) {
        if (connection.status & CONN_CLOSED) {
            break;
        }

        const auto limit_rate = this->config->speed_limit_rate().load();
        if (connection.stats.transfared_last_second >= limit_rate) {
            connection.iomutex.unlock();
            unlock.disable();
            connection.status.fetch_or(CONN_LIMIT_RATE);
            co_await this->limit_rate_cv->wait([&] ()
                -> bool { return connection.status & CONN_CLOSED || connection.stats.transfared_last_second < limit_rate; });
            connection.status.fetch_xor(CONN_LIMIT_RATE);
            if (!connection.iomutex.try_to_lock()) {
                co_return -1;
            }
            unlock.enable();
        }

        size = std::min(limit_rate - connection.stats.transfared_last_second.load(), size);

        connection.stats.transfared_last_second.fetch_add(size);

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
                co_await async::read_ready (this->site.async_context(), connection.id, &connection.rcancel, [&] ()
                    -> void { connection.iomutex.unlock(); unlock.disable(); });
                connection.status.fetch_xor(CONN_READ);

                if (!connection.iomutex.try_to_lock()) {
                    co_return -1;
                }
                unlock.enable();
                continue;
            }
            break;
        }
        connection.stats.total_read.fetch_add(rhs);
        co_return rhs;
    }
    co_return -1;
}
