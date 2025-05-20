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

#include "http/HTTPv2.hpp"
#include "worker/HTTPv2.hpp"

#include "ManapiUtils.hpp"
#include "async/ManapiAsyncSocket.hpp"
#include "http/base_http.hpp"

#include "ManapiHttpRequest.hpp"
#include "ManapiHttpResponse.hpp"

manapi::net::worker::TCP::TCP(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata) : base (std::move(site), std::move(wdata)) {
    this->local = nullptr;
}

manapi::net::worker::TCP::~TCP() {
    if (this->local) {
        freeaddrinfo(this->local);
    }
}

bool manapi::net::worker::TCP::is_valid_connection(worker::connection *connection) {
    return connection->len;
}

void manapi::net::worker::TCP::init() {
    try {
        addrinfo hints = {
            .ai_family      = PF_UNSPEC,
            .ai_socktype    = SOCK_STREAM,
            .ai_protocol    = IPPROTO_TCP
        };

        auto &address = this->config()->address();
        auto &port = this->config()->port();

        if (getaddrinfo(address.data(), port.data(), &hints, &this->local) != 0) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "failed to resolve host");
        }

        this->config()->server_address(*this->local->ai_addr);
        this->config()->server_len(this->local->ai_addrlen);

        MANAPIHTTP_LOG("HTTP TCP PORT USED: {}. {}:{}", port, address, port);

        /* every 1 second */
        this->limit_rate_timer = manapi::async::current()->timerpool()->append_interval_sync(1000,
            [this] (manapi::timer t) -> void { this->update_limit_rate(); });

        this->watcher_accept_ = manapi::async::current()->eventloop()->create_watcher_tcp_accept(
            [this] (std::shared_ptr<ev::tcp> & w, int status)
            -> void {
                this->onaccept(w, status);
            });

        memset(&this->sockaddrin, '\0', sizeof (sockaddr));

        if (this->local->ai_family == ev::IPv4) {
            if (auto rhs = this->watcher_accept_->ip4_addr(this->config()->address().data(), std::stoi(this->config()->port()), reinterpret_cast<sockaddr_in *>(&this->sockaddrin))) {
                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_SOCKET, "couldn't set ipv4 addr due to result - {}", rhs);
                goto err;
            }
        }
        else if (this->local->ai_family == ev::IPv6) {
            if (auto rhs = this->watcher_accept_->ip6_addr(this->config()->address().data(), std::stoi(this->config()->port()), reinterpret_cast<sockaddr_in6 *>(&this->sockaddrin))) {
                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_SOCKET, "couldn't set ipv6 addr due to result - {}", rhs);
                goto err;
            }
        }

        if (auto rhs = this->watcher_accept_->nodelay(this->config()->tcp_no_delay())) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't set nodelay due to result - {}", rhs);
            goto err;
        }

        if (auto rhs = this->watcher_accept_->simultaneous_accepts(this->config()->simultaneous_accepts())) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't set simultaneous_accepts due to result - {}", rhs);
            goto err;
        }

        if (auto rhs = this->watcher_accept_->keepalive(!!this->config()->keep_alive(), this->config()->keep_alive())) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't set keep-alive due to result - {}", rhs);
            goto err;
        }

        if (auto rhs = this->watcher_accept_->s_bind(reinterpret_cast<sockaddr *> (&this->sockaddrin), ev::TCP_REUSEPORT)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't bind socket due to result - {}", rhs);
            goto err;
        }

        if (auto rhs = this->watcher_accept_->listen(this->config()->max_backlog())) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't listen socket due to result - {}", rhs);
            goto err;
        }

        this->watcher_accept_->read_start();

        this->http_v2_worker = std::make_shared<net::worker::http_v2>(this->site(),
            this->bufferpool(), this->worker_data());
        this->http_v2_worker->config(this->config());
        return;
    }
    catch (std::exception const &e) {
        manapi::async::current()->logger()->error(manapi::logger::default_service,
            ERR_SOCKET, "tcp: init failed(...) due to {}", e.what());
    }
err:
    THROW_MANAPIHTTP_EXCEPTION2 (ERR_SOCKET, "tcp: init(...) failed");
}

void manapi::net::worker::TCP::configure_connection(const worker::shared_conn &connection, oncont_cb cb) {
    cb.call(true);
}

manapi::future<ssize_t> manapi::net::worker::TCP::response(const worker::shared_conn &connection, http::response *resp, bool finish) {
    static constexpr std::string delimiter = "\r\n";
    const auto response = manapi::net::worker::TCP::stringify_http_info(resp, connection->version, delimiter) + this->stringify_headers(resp, delimiter) + delimiter;

    const auto rhs = co_await this->write (connection, response.data(), response.size(), finish);
    co_return rhs;
}

void manapi::net::worker::TCP::onaccept(std::shared_ptr<ev::tcp> &watcher, int status) {
    if (status) {
        return;
    }

    shared_conn connection;
    try {
        // if (this->config()->max_connections() <= this->connections.size()) {
        //     return;
        // }

        connection = this->accept(watcher);

        if (!connection) {
            return;
        }

        auto id = reinterpret_cast<std::uintptr_t> (connection.get());
        auto it = this->connections.insert({id, connection});
        assert((it.second && "connection couldn't be saved due to duplicate"));

        this->onaccept_event_(connection);

        return;
    }
    catch (...) {
        /* error */
    }

    if (connection) {
        this->close_connection(connection, false);
    }
}

void manapi::net::worker::TCP::onrecv(std::shared_ptr<ev::tcp> &watcher, const worker::shared_conn &conn, ibuffpool_t buffer) {
    auto connection = conn->as<connection_interface>();
    connection->ev_callback->operator()(conn, ev::READ, buffer->data(), buffer->size());
}

std::shared_ptr<manapi::net::worker::TCP> manapi::net::worker::TCP::create(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::TCP>(std::move(site), std::move(wdata));
    worker->config(config.get());
    worker->self_ = std::weak_ptr (worker);
    return std::move(worker);
}

manapi::net::worker::shared_conn manapi::net::worker::TCP::accept (ev::shared_tcp &w, std::move_only_function<shared_conn()> init) {
    auto connection = init();

    ev::shared_tcp client = manapi::async::current()->eventloop()->create_watcher_tcp_connection(
        [this, wconnection = std::weak_ptr(connection)] (std::shared_ptr<ev::tcp> &w, ssize_t nread, const uv_buf_t *buf)
        -> void {
            try {
                auto connection = wconnection.lock();

                if (!nread) {
                    return;
                }

                if (nread < 0) {
                    this->close_connection(connection, false);
                    return;
                }

                auto object = this->bufferpool()->get(std::make_unique<bytebuffer>(buf->base, buf->len));
                object->resize(nread);
                this->onrecv(w, connection, std::move(object));
            }
            catch (std::exception const &e) {
                manapi::async::current()->logger()->error(manapi::logger::default_service,
                    manapi::ERR_FATAL, "tcp: onrecv(...) unexpected error: {}", e.what());
            }
    }, [this] (std::shared_ptr<ev::tcp> &, size_t suggested_size, ev::buff_t *buff) -> void {
        auto buffer = this->bufferpool()->get();
        buffer->resize(suggested_size);
        auto object = buffer.release();

        buff->len = object->realsize();
        buff->base = static_cast<char *>(object->release());
    });


    if (client->accept(w.get())) {
        manapi::async::current()->eventloop()->stop_watcher_tcp_connection(std::move(client), nullptr);
        return nullptr;
    }

    auto keepalive = this->config()->keep_alive();
    if (auto rhs = client->keepalive(!!keepalive, keepalive)) {
        manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't set keep-alive due to result - {}", rhs);
        manapi::async::current()->eventloop()->stop_watcher_tcp_connection(std::move(client), nullptr);
        return nullptr;
    }

    this->worker_data_->count.fetch_add(1);

    int addrlen = sizeof (connection->client.data);
    if (auto rhs = client->getpeername(reinterpret_cast <sockaddr *>(connection->client.data), &addrlen)) {
        std::cerr << "getpeername failed " << rhs << "\n";
    }
    connection->len = addrlen;


    auto conn = connection->as<connection_interface>();

    conn->top = std::make_unique<connection_io>();

    conn->worker = this->self_.lock();
    conn->watcher = std::move(client);

    conn->t = manapi::async::current()->timerpool()->append_interval_sync(15000,
        [wconnection = std::weak_ptr(connection)] (manapi::timer t) mutable -> void {
            auto connection = wconnection.lock();
            dynamic_cast<TCP*>(connection->as<connection_interface>()->worker.get())->timeout_(connection);
        });

    return std::move(connection);
}

manapi::net::worker::shared_conn manapi::net::worker::TCP::accept(ev::shared_tcp &w) {
    return std::move(this->accept(w,
        [this] () -> shared_conn {
        return std::make_shared<worker::connection> (new connection_interface (), connection_interface_eraser);
    }));
}

void manapi::net::worker::TCP::close_connection(const shared_conn &conn, bool clean_disconnect) {
    auto connection = conn->as<connection_interface>();

    if (connection->status & CONN_CLOSED) {
        return;
    }

    connection->status |= CONN_CLOSED;

    if (connection->watcher && !clean_disconnect) {
        if (connection->t) {
            connection->t.stop();
            connection->t = nullptr;
        }
        if (connection->ev_callback) {
            connection->ev_callback->operator()(conn, ev::DISCONNECT, nullptr, 0);
        }
        connection->watcher->read_stop();
        manapi::async::current()->eventloop()->stop_watcher_tcp_connection(std::move(connection->watcher),
            std::make_unique<ev::tcp_close_cb>([] (const ev::shared_tcp &tcp) -> void {

            }));
        this->connections.erase(reinterpret_cast<uintptr_t> (conn.get()));
    }
    else {
        if (connection->t) {
            connection->t.stop();
            connection->t = manapi::async::current()->timerpool()->append_interval_sync(15000,
            [conn] (manapi::timer t) mutable -> void {
                dynamic_cast<TCP*>(conn->as<connection_interface>()->worker.get())->timeout_(conn);
            });
        }
    }
}

void manapi::net::worker::TCP::stop() {
    /* on the libev main loop */
    this->limit_rate_timer.stop();
}

void manapi::net::worker::TCP::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size) {
    auto const data = conn->as<connection_interface>();
    if (data->ev_callback) {
        data->ev_callback->operator()(conn, flags, buff, size);
    }
}

ssize_t manapi::net::worker::TCP::sync_write_ex(const worker::shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt) {
    auto connection = conn->as<connection_interface>();

    if (connection->status & CONN_CLOSED) {
        return CONN_IO_ERROR;
    }

    ssize_t rhs;

    if (connection->top->send_size) {
        rhs = 0;
    }
    else {
        rhs = connection->watcher->try_write(buff, size);

        if (rhs < 0) {
            rhs = 0;
        }
    }


    if (rhs != size) {
        rhs += connection_io_send (&connection->top->send, static_cast<const char *>(buff) + rhs, size - rhs, this->bufferpool().get(),
            this->config_->buffer_size(), &connection->top->send_size, maxcnt);

        this->flush_write_(conn, finish);
    }

    return rhs;
}

ssize_t manapi::net::worker::TCP::sync_write(const worker::shared_conn &conn, const void *buff, ssize_t size, bool finish) {
    return sync_write_ex(conn, buff, size, finish, this->config_->max_buffer_stack());
}

std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::TCP::event_on(const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) {
    auto const conn_data = conn->as<connection_interface>();
    auto n = std::exchange(conn_data->ev_callback, std::move(callback));
    return std::move(n);
}

int manapi::net::worker::TCP::event_flags(const shared_conn & conn, int flags) {
    auto const data = conn->as<connection_interface>();
    auto &status = data->status;
    if ((flags & ev::READ)) {
        if (!(status & ev::READ)) {
            assert(!data->watcher->read_start());
        }
    }
    else {
        if (status & ev::READ) {
            assert(!data->watcher->read_stop());
        }
    }

    return std::exchange(status, ((status >> 2) << 2) | flags);
}

int manapi::net::worker::TCP::event_flags(const shared_conn & conn) {
    return (conn->as<connection_interface>()->status);
}

void manapi::net::worker::TCP::flush_write_(const worker::shared_conn &connection, bool flush) {
    auto conn = connection->as<connection_interface>();
    bool flg = false;

    while (conn->top->send.last_deque && ((conn->top->send.deque.get() != conn->top->send.last_deque
        || (conn->top->send.deque->buffer->size() == conn->top->send.deque_cursor)) || flush)) {
        flg = true;

        auto object = std::move(conn->top->send.deque->buffer);
        conn->top->send.deque = std::move(conn->top->send.deque->next);

        if (!conn->top->send.deque) {
            conn->top->send.last_deque = nullptr;
            object->resize(conn->top->send.deque_cursor);
            conn->top->send.deque_cursor = 0;
        }

        if (conn->top->send.deque_current) {
            object->shift_add(conn->top->send.deque_current);
            conn->top->send.deque_current = 0;
        }


        conn->stats.transfared_last_second+=static_cast<int>(object->size());

        auto s = std::make_unique<ev::buff_t>(object->data(), static_cast<std::size_t>(object->size()));

        auto w = manapi::async::current()->eventloop()
            ->create_watcher_write(conn->watcher.get(), [connection, b = std::move(object), s = std::move(s)]
                (std::shared_ptr<ev::write> &w, int status)
                -> void {
                auto conn = connection->as<connection_interface>();

                conn->top->send_size--;

                if (status) {
                    /* error */
                    conn->status |= ev::DISCONNECT;

                    if (conn->ev_callback) {
                        conn->ev_callback->operator()(connection, ev::DISCONNECT, nullptr, 0);
                    }
                }
                else {
                    if (conn->status & ev::WRITE) {
                        conn->ev_callback->operator()(connection, ev::WRITE, nullptr, 0);
                    }
                }

                manapi::async::current()->eventloop()->stop_watcher(w);
            }, s.get(), 1 /* nbuf */);
    }

    if (flg) {

    }
}

void manapi::net::worker::TCP::update_limit_rate() {
    /* in the event loop */
    for (auto &conn: this->connections) {
        this->update_limit_rate_connection(conn.second.get());
    }
}

void manapi::net::worker::TCP::timeout_(shared_conn conn) {
    auto data = conn->as<connection_interface>();
goto err;

    if (data->status & ev::DISCONNECT) {
        goto err;
    }
    if (data->status & (ev::READ|ev::WRITE)) {
        if (data->stats.transfared_last_second < this->config()->speed_check_bytes()) {
            goto err;
        }
    }
    return;
err:
    data->t.stop();
    data->status |= (ev::DISCONNECT);

    this->close_connection(conn, false);

    return;
}

void manapi::net::worker::TCP::ev_watcher_stop_(connection_interface &conn) {
    this->connections.erase(reinterpret_cast<std::uintptr_t>(conn.watcher.get()));
}

void manapi::net::worker::TCP::update_limit_rate_connection(connection *conn) {
    auto conn_data = conn->as<connection_interface>();
    conn_data->stats.transfared_last_second=(0);

}

void manapi::net::worker::TCP::http2_work_(http::http_v2_t *http_v2_ctx, const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize) {
    int rhs;

    if (flags & ev::DISCONNECT) {
        goto err;
    }

    if (flags & ev::WRITE) {
        if (http::http_v2_on_write(http_v2_ctx)) {
            goto err;
        }
    }

    if (flags & ev::READ) {
        while (true) {
            rhs = http::http_v2_work(http_v2_ctx, this->config(), &buffer, &nsize);

            switch (rhs) {
                case http::EHTTP_V2_PROTOCOL_OK: {
                    http::http_v2_on_close (http_v2_ctx);
                    this->close_connection(conn, true);
                    break;
                }
                case http::EHTTP_V2_PROTOCOL_WANT_READ: {
                    break;
                }
                case http::EHTTP_V2_PROTOCOL_ERROR: {
                    goto err;
                }
                case http::EHTTP_V2_IO_ERROR: {
                    goto err;
                }
                case http::EHTTP_V2_NEW_STREAM: {
                    /**
                     * stream id always must be at the end
                     * of the map (ctx->streams).
                     **/
                    auto const s = http_v2_ctx->streams->rbegin();
                    assert((s != http_v2_ctx->streams->rend() && "At least one stream must be"));

                    auto const sdata = s->second->as<http::http_v2_stream_t>();
                    auto const req_ptr = sdata->req.get();
                    auto cdata = std::make_unique<http::internal::handle_data_t>(s->second, this->http_v2_worker,
                        req_ptr, std::make_unique<http::internal::cont_callback_cb_t>(
                        [this, sconn = s->second, conn, req = std::move(sdata->req)] (bool ok)
                        -> void {
                            auto data = sconn->as<http::http_v2_stream_t>();

                            if (ok) {
                                if (this->config()->keep_alive()) {
                                    this->http_v2_worker->close_connection(sconn, true);
                                }
                                else {
                                    this->http_v2_worker->close_connection(sconn, true);
                                }
                            }
                            else {
                                /* failed */
                                this->http_v2_worker->close_connection(sconn, false);
                            }

                    }));

                    // this->event_on(conn, std::unique_ptr<worker_watcher_cb>(nullptr));
                    // this->event_flags(conn, 0);

                    net::http::internal::handle_income_request(std::move(cdata), this->site().handler(req_ptr), http::OK_200);

                    continue;
                }
                default: {
                    goto err;
                }
            }

            break;
        }
    }

    return;
    err: {
        this->close_connection(conn, false);
        if (http::http_v2_on_close (http_v2_ctx)) {
            /* error */
        }
    }
}

bool manapi::net::worker::TCP::is_writable(const shared_conn &conn) {
    auto const data = conn->as<connection_interface>();
    return data->top->recv_size <= this->config_->max_buffer_stack();
}

std::string manapi::net::worker::TCP::stringify_http_info(manapi::net::http::response *res, const int &version, const std::string &delimiter) {
    return "HTTP/" + http::config::stringify_http_version(version) + ' ' + std::to_string(res->status_code()) + (version < http::versions::HTTP_v2 ? ' ' + std::string{res->status_message()} + delimiter : delimiter);
}

std::string manapi::net::worker::TCP::stringify_headers(manapi::net::http::response *res, const std::string &delimiter) {
    std::string data;

    std::size_t size = 0;
    for (const auto &header : res->ref_headers()) {
        size += header.first.size() + (sizeof (": ") - 1) + header.second.size() + delimiter.size();
    }
    data.reserve(size);

    // add headers
    for (const auto &header: res->ref_headers()) {
        data += header.first + ": " + header.second + delimiter;
    }

    return data;
}

void manapi::net::worker::TCP::connection_interface_eraser(void *ptr) {
    auto connection = static_cast<connection_interface *> (ptr);

    if (connection->watcher) {
        connection->watcher->read_stop();
        manapi::async::current()->eventloop()->stop_watcher_tcp_connection(std::move(connection->watcher),
            std::make_unique<ev::tcp_close_cb>([] (const ev::shared_tcp &tcp) -> void {

            }));
    }

    //std::cout << "CLOSE\n";
    connection->worker->worker_data()->count.fetch_sub(1);
    delete connection;
}

void manapi::net::worker::TCP::http_work_(http::http_v1_1_t *http_v1_1_ctx, const worker::shared_conn &conn, int flags, const char *buff, ssize_t size) {
    auto data = conn->as<connection_interface>();

    if (flags & ev::DISCONNECT) {
        goto err;
    }


    try {
        if (flags & ev::READ) {

            switch (http::http_v1_1_work(http_v1_1_ctx, this->config(), &buff, &size)) {
                case http::EHTTP_V1_1_PROTOCOL_OK: {
                    auto req_ptr = http_v1_1_ctx->req.get();
                    if (size) {
                        auto buffer = this->bufferpool()->get();

                        buffer->resize(size);
                        memcpy (buffer->data(), buff, size);

                        req_ptr->buffer = std::move(buffer);
                        req_ptr->buffer->shift_add(req_ptr->buffer->size() - size);
                    }

                    auto const exceptheader = req_ptr->headers.find(http::HEADER.EXPECT);
                    if (exceptheader != req_ptr->headers.end()) {
                        ssize_t const copy = sizeof ("HTTP/1.1 100 Continue\r\n\r\n") - 1;
                        auto const rhs = sync_write_ex (conn, static_cast<const char *>("HTTP/1.1 100 Continue\r\n\r\n"),
                            copy, true, 1e5);
                        if (copy != rhs) {
                            goto err;
                        }
                    }

                    auto cdata = std::make_unique<http::internal::handle_data_t>(conn, data->worker, req_ptr, std::make_unique<http::internal::cont_callback_cb_t>(
                        [this, conn, req = std::move(http_v1_1_ctx->req)] (bool ok)
                        -> void {
                            auto data = conn->as<connection_interface>();

                            if (ok) {
                                if (this->config()->keep_alive()) {
                                    this->onaccept_event_(conn);
                                    if (req->buffer != nullptr && !req->buffer->empty()) {
                                        this->onrecv(data->watcher, conn, std::move(req->buffer));
                                    }
                                    else {
                                        data->worker->close_connection(conn, true);
                                    }
                                }
                                else {
                                    data->worker->close_connection(conn, true);
                                }
                            }
                            else {
                                /* failed */
                                data->worker->close_connection(conn, false);
                            }

                    }));

                    this->event_on(conn, std::unique_ptr<worker_watcher_cb>(nullptr));
                    this->event_flags(conn, 0);

                    connection_io_send(&data->top->recv, buff, size,
                        this->bufferpool().get(), this->config_->buffer_size(), &data->top->recv_size, 1e5);

                    net::http::internal::handle_income_request(std::move(cdata), this->site().handler(req_ptr), http::OK_200);

                    break;
                }
                case http::EHTTP_V1_1_PROTOCOL_UPGRADE: {
                    auto httpv = http_v1_1_ctx->http;

                    this->event_on(conn, std::unique_ptr<worker_watcher_cb>(nullptr));
                    this->event_flags(conn, 0);

                    switch (httpv) {
                        case http::versions::HTTP_v0_9: {
                            goto err;
                        }
                        case http::versions::HTTP_v1_0: {
                            goto err;
                        }
                        case http::versions::HTTP_v1_1: {
                            goto err;
                        }
                        case http::versions::HTTP_v2: {
                            auto http2_ctx = std::make_unique<http::http_v2_t>();
                            http2_ctx->conn = conn;
                            http2_ctx->worker = this;
                            http2_ctx->http_v2_worker = this->http_v2_worker;

                            this->event_on(conn, std::make_unique<worker_watcher_cb>(
                                [this, ctx = std::move(http2_ctx)]
                                (const shared_conn & conn, int flags, const char *buffer, ssize_t nsize)
                                -> void {
                                this->http2_work_(ctx.get(), conn, flags, buffer, nsize);
                            }));

                            this->event_flags(conn, ev::READ|ev::WRITE);

                            break;
                        }
                        case http::versions::HTTP_v3: {
                            /* i got you, bro */
                            goto err;
                        }
                        default: {
                            goto err;
                        }
                    }

                    connection_io_send(&data->top->recv, buff, size,
                        this->bufferpool().get(), this->config_->buffer_size(), &data->top->recv_size, 1e5);

                    break;
                }
                case http::EHTTP_V1_1_PROTOCOL_ERROR: {
                    goto err;
                }
                case http::EHTTP_V1_1_PROTOCOL_WANT_READ: {
                    /* skip */
                    break;
                }
                default: {
                    perror("An invalid state for http_v1_1_work()");
                }
            }
        }

        return;
    }
    catch (...) {
        /* fatal error */
    }

    err: this->close_connection(conn, false);
}

void manapi::net::worker::TCP::onaccept_event_(const worker::shared_conn &conn) {
    auto http_v1_1_ctx = std::make_unique<http::http_v1_1_t>();

    this->event_on(conn,
        std::make_unique<worker_watcher_cb>([this, http_v1_1_ctx = std::move(http_v1_1_ctx)]
        (const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize) mutable
        -> void {
            this->http_work_ (http_v1_1_ctx.get(), conn, flags, buffer, nsize);
    }));

    this->event_flags(conn, ev::READ);
}
