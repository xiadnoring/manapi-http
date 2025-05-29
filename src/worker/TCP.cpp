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
#include "ManapiString.hpp"

manapi::net::worker::TCP::TCP(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, manapi::net::http::config *config) : base (std::move(site), std::move(wdata), config) {
    this->local = nullptr;
    this->finish = nullptr;
    this->flags = 0;
    this->count = 0;
}

manapi::net::worker::TCP::~TCP() {
    if (this->local) {
        freeaddrinfo(this->local);
    }

    if (this->limit_rate_timer) {
        this->limit_rate_timer.stop();
        this->limit_rate_timer.clear();
        this->limit_rate_timer = nullptr;
    }
}

bool manapi::net::worker::TCP::is_valid_connection(worker::connection *connection) {
    return !!connection->ipdata;
}

void manapi::net::worker::TCP::init() {
    try {
        addrinfo hints = {
            .ai_family      = PF_UNSPEC,
            .ai_socktype    = SOCK_STREAM,
            .ai_protocol    = IPPROTO_TCP
        };

        auto &address = this->config_->address;
        auto &port = this->config_->port;

        if (getaddrinfo(address.data(), port.data(), &hints, &this->local) != 0) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "failed to resolve host");
        }

        this->config_->server_len=(this->local->ai_addrlen);
        memcpy (&this->config_->server_addr,this->local->ai_addr, this->local->ai_addrlen);

        MANAPIHTTP_LOG("HTTP TCP PORT USED: {}. {}:{}", port, address, port);

        /* every 1 second */
        this->limit_rate_timer = manapi::async::current()->timerpool()->append_interval_sync(1000,
            [this] (const manapi::timer& t) -> void { this->update_limit_rate(); });

        this->watcher_accept_ = manapi::async::current()->eventloop()->create_watcher_tcp_accept(
            [this] (std::shared_ptr<ev::tcp> & w, int status)
            -> void {
                this->onaccept(w, status);
            });

        memset(&this->sockaddrin, '\0', sizeof (sockaddr));

        if (this->local->ai_family == ev::IPv4) {
            if (auto rhs = this->watcher_accept_->ip4_addr(this->config_->address.data(), std::stoi(this->config_->port), reinterpret_cast<sockaddr_in *>(&this->sockaddrin))) {
                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_SOCKET, "couldn't set ipv4 addr due to result - {}", rhs);
                goto err;
            }
        }
        else if (this->local->ai_family == ev::IPv6) {
            if (auto rhs = this->watcher_accept_->ip6_addr(this->config_->address.data(), std::stoi(this->config_->port), reinterpret_cast<sockaddr_in6 *>(&this->sockaddrin))) {
                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_SOCKET, "couldn't set ipv6 addr due to result - {}", rhs);
                goto err;
            }
        }

        if (auto rhs = this->watcher_accept_->nodelay(this->config_->tcp_no_delay)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't set nodelay due to result - {}", rhs);
            goto err;
        }

        if (auto rhs = this->watcher_accept_->simultaneous_accepts(this->config_->simultaneous_accepts)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't set simultaneous_accepts due to result - {}", rhs);
            goto err;
        }

        if (auto rhs = this->watcher_accept_->keepalive(!!this->config_->keep_alive, this->config_->keep_alive)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't set keep-alive due to result - {}", rhs);
            goto err;
        }

        if (auto rhs = this->watcher_accept_->s_bind(reinterpret_cast<sockaddr *> (&this->sockaddrin), ev::TCP_REUSEPORT)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't bind socket due to result - {}", rhs);
            goto err;
        }

        if (auto rhs = this->watcher_accept_->listen(this->config_->max_backlog)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't listen socket due to result - {}", rhs);
            goto err;
        }


        this->http_v2_worker = std::make_shared<net::worker::http_v2>(this->site(),
            this->bufferpool(), this->worker_data(), this->config_);
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
        // if (this->config_->max_connections() <= this->connections.size()) {
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
    auto const connection = conn->as<connection_interface>();
    auto const size = static_cast<int>(buffer->size());
    if (connection->status & CONN_LIMIT_RATE) {
        connection->transfered += size;
        if (connection->transfered >= this->config_->speed_limit_rate) {
            connection->watcher->read_stop();
        }
    }


    tcp_handle_read_data(conn, connection, ev::READ, buffer->data(), size, &buffer);
}

std::shared_ptr<manapi::net::worker::TCP> manapi::net::worker::TCP::create(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::TCP>(std::move(site), std::move(wdata), config.get());
    worker->self_ = std::weak_ptr (worker);
    return std::move(worker);
}

manapi::net::worker::shared_conn manapi::net::worker::TCP::accept (ev::shared_tcp &w, std::move_only_function<shared_conn()> init) {
    auto connection = init();

    ev::shared_tcp client = manapi::async::current()->eventloop()->create_watcher_tcp_connection(
        [this, weak = std::weak_ptr(connection)] (std::shared_ptr<ev::tcp> &w, ssize_t nread, const uv_buf_t *buf)
        -> void {
            try {
                auto const connection = weak.lock();

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
        manapi::async::current()->eventloop()->stop_watcher_tcp_connection(std::move(client));
        return nullptr;
    }

    if (auto rhs = client->keepalive(!!this->config_->keep_alive, this->config_->keep_alive)) {
        manapi::async::current()->logger()->error(logger::default_service, ERR_SOCKET, "couldn't set keep-alive due to result - {}", rhs);
        manapi::async::current()->eventloop()->stop_watcher_tcp_connection(std::move(client));
        return nullptr;
    }

    this->count++;
    this->worker_data_->count.fetch_add(1);

    if (this->count >= this->config_->max_connections) {
        // TODO: stop accepting
    }

    connection->ipdata = std::make_unique<decltype(connection)::element_type::ipdata_t>();
    connection->ipdata->len = 0;

    int addrlen = sizeof (connection->ipdata->client.data);
    if (auto rhs = client->getpeername(reinterpret_cast <sockaddr *>(connection->ipdata->client.data), &addrlen)) {
        std::cerr << "getpeername failed " << rhs << "\n";
    }
    connection->ipdata->len = addrlen;


    auto conn = connection->as<connection_interface>();

    conn->top = std::make_unique<connection_io>();

    conn->worker = this;
    conn->watcher = std::move(client);

    return std::move(connection);
}

manapi::net::worker::shared_conn manapi::net::worker::TCP::accept(ev::shared_tcp &w) {
    return std::move(this->accept(w,
        [this] () -> shared_conn {
        return std::make_shared<worker::connection> (new connection_interface{}, connection_interface_eraser);
    }));
}

void manapi::net::worker::TCP::close_connection(shared_conn conn, bool clean_disconnect) {
    auto connection = conn->as<connection_interface>();

    if (connection->status & CONN_REMOVED) {
        return;
    }

    conn->cancellation.cancel();

    connection->data = nullptr;
    if (connection->watcher && !clean_disconnect) {
        if (connection->status & CONN_KEEP_ALIVE) {
            connection->status ^= CONN_KEEP_ALIVE;
        }

        connection->status = CONN_REMOVED|CONN_CLOSED;

        if (connection->t) {
            connection->t.stop();
            connection->t.clear();
            connection->t = nullptr;
        }
        if (connection->ev_callback) {
            connection->ev_callback->operator()(conn, ev::DISCONNECT, nullptr, 0, nullptr);
        }
        connection->watcher->read_stop();
        manapi::async::current()->eventloop()->stop_watcher_tcp_connection(std::move(connection->watcher));

        this->connections.erase(reinterpret_cast<uintptr_t> (conn.get()));
    }
    else {
        conn->cancellation.reset();

        connection->status |= CONN_KEEP_ALIVE;
        if (connection->t) {
            connection->t.stop();
            connection->t.clear();
        }
        connection->t = manapi::async::current()->timerpool()->append_interval_sync(15000,
            [conn] (manapi::timer t) mutable -> void {
            dynamic_cast<TCP*>(conn->as<connection_interface>()->worker)->timeout_(conn);
        });
    }
}

void manapi::net::worker::TCP::stop(std::function<void()> cb) {
    if (this->watcher_accept_) {
        manapi::async::current()->eventloop()
            ->stop_callback<ev::tcp>(this->watcher_accept_, [this, cb = std::move(cb)] (const ev::shared_tcp &w) -> void {
                this->flags |= NET_WORKER_CLOSED;
                this->finish = std::move(cb);

                if (!this->count) {
                    this->finish();
                }
            });
        manapi::async::current()->eventloop()
            ->stop_watcher_tcp_accept(std::move(this->watcher_accept_));
    }
}

void manapi::net::worker::TCP::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) {
    auto const data = conn->as<connection_interface>();
    if (data->ev_callback) {
        data->ev_callback->operator()(conn, flags, buff, size, p);
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
        else {
            connection->transfered += rhs;
        }
    }


    if (rhs != size) {
        rhs += connection_io_send (&connection->top->send, static_cast<const char *>(buff) + rhs, size - rhs, this->bufferpool().get(),
            this->config_->buffer_size, &connection->top->send_size, maxcnt);

        connection->transfered += rhs;

        this->flush_write_(conn, finish);
    }

    return rhs;
}

ssize_t manapi::net::worker::TCP::sync_write(const worker::shared_conn &conn, const void *buff, ssize_t size, bool finish) {
    auto const connection = conn->as<connection_interface>();
    size = std::min(size, this->config_->speed_limit_rate - connection->transfered);

    if (size <= 0) {
        return 0;
    }

    return sync_write_ex(conn, buff, size, finish, this->config_->max_buffer_stack);
}

std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::TCP::event_on(const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) {
    auto const conn_data = conn->as<connection_interface>();
    auto n = std::exchange(conn_data->ev_callback, std::move(callback));
    return std::move(n);
}

int manapi::net::worker::TCP::event_flags(const shared_conn & conn, int flags) {
    auto const data = conn->as<connection_interface>();
    auto &status = data->status;


    data->speed_min_delay = static_cast<int>(this->config_->speed_check_delay);

    auto const prev = std::exchange(status, ((status >> 2) << 2) | flags);

    if (status & CONN_HTTP_1_1_CHUNKED && status & ev::READ) {
        /* flush chunked data */
        switch (http::http_v1_1_chunked_flush(static_cast<TCP::connection_data_t *> (data->data)->chunked_ctx.get(),
            this, conn)) {
            case http::EHTTP_V1_1_CHUNKED_OK: {
                data->status |= CONN_RECV_END;
                this->feed_event(conn, CONN_RECV_END, nullptr, 0, nullptr);
                break;
            }
            case http::EHTTP_V1_1_CHUNKED_ERR: {
                this->close_connection(conn, false);
                break;
            }
            case http::EHTTP_V1_1_CHUNKED_READ: {
                break;
            }
            case http::EHTTP_V1_1_CHUNKED_WAIT: {
                break;
            }
        }
    }

    if ((status & ev::READ)) {
        flush_read_ (conn, data);

        if (!(prev & (ev::READ|ev::DISCONNECT)) && !data->watcher->is_active()) {
            assert(!data->watcher->read_start());
        }
    }
    else {
        if ((prev & ev::READ ) && !(prev & ev::DISCONNECT) && data->watcher->is_active()) {
            assert(!data->watcher->read_stop());
        }
    }

    if ((status & CONN_RECV_END) && (status & CONN_READ) && data->ev_callback) {
        data->ev_callback->operator()(conn, CONN_RECV_END, nullptr, 0, nullptr);
    }

    return prev;
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
                    connection->cancellation.cancel();

                    if (conn->ev_callback) {
                        conn->ev_callback->operator()(connection, ev::DISCONNECT, nullptr, 0, nullptr);
                    }
                }
                else {
                    if (conn->status & ev::WRITE) {
                        conn->ev_callback->operator()(connection, ev::WRITE, nullptr, 0, nullptr);
                    }
                }

                manapi::async::current()->eventloop()->stop_watcher(w);
            }, s.get(), 1 /* nbuf */);
    }

    if (flg) {

    }
}

void manapi::net::worker::TCP::flush_read_(const shared_conn &conn, connection_interface *data) {
    if (data->top->recv_size) {
        while (data->top->recv.last_deque
            && (data->status & ev::READ)
            && data->ev_callback) {
            auto object = std::move(data->top->recv.deque->buffer);
            data->top->recv.deque = std::move(data->top->recv.deque->next);

            if (!data->top->recv.deque) {
                data->top->recv.last_deque = nullptr;
                object->resize(data->top->recv.deque_cursor);
                data->top->recv.deque_cursor = 0;
            }

            if (data->top->recv.deque_current) {
                object->shift_add(data->top->recv.deque_current);
                data->top->recv.deque_current = 0;
            }

            data->top->recv_size--;

            if (!object->empty()) {
                tcp_handle_read_data(conn, data, ev::READ, object->data(), object->size(), &object);
            }
        }
    }
}

void manapi::net::worker::TCP::tcp_handle_read_data(const shared_conn &conn, connection_interface *data, int flags, const char *buffer, ssize_t size, ibuffpool_t *p) {
    if (data->status & CONN_HTTP_1_1_CHUNKED) {
        switch (auto rhs = http::http_v1_1_chunked_read(static_cast<connection_data_t *>(data->data)->chunked_ctx.get(),
            this, conn, this->config_, buffer, size)) {
            case http::EHTTP_V1_1_CHUNKED_OK: {
                data->status |= CONN_RECV_END;
                this->feed_event(conn, CONN_RECV_END, nullptr, 0, nullptr);
                break;
            }
            case http::EHTTP_V1_1_CHUNKED_READ: {
                break;
            }
            case http::EHTTP_V1_1_CHUNKED_ERR:
                default: {
                /* error */
                this->close_connection(conn, false);
                break;
                }
            }
    }
    else {
        data->ev_callback->operator()(conn, ev::READ, buffer, size, p);
    }
}

void manapi::net::worker::TCP::update_limit_rate() {
    /* in the event loop */
    auto it = this->connections.begin();
    if (it == this->connections.end()) {
        return;
    }
    while (this->update_limit_rate_connection(it->second)) {
        /* was removed */
        it = this->connections.begin();
        if (it == this->connections.end()) {
            return;
        }
    }
    for (auto nit = std::next(it); nit != this->connections.end(); nit = std::next(it)) {
        if(!this->update_limit_rate_connection(nit->second)) {
            it = nit;
        }
    }
}

void manapi::net::worker::TCP::timeout_(shared_conn conn) {
    auto const data = conn->as<connection_interface>();

    if (data->t) {
        data->t.stop();
        data->t.clear();

        data->t = nullptr;
    }

    data->status |= (ev::DISCONNECT);

    if (data->status & CONN_KEEP_ALIVE) {
        data->status ^= CONN_KEEP_ALIVE;
    }

    this->close_connection(conn, false);
}

void manapi::net::worker::TCP::ev_watcher_stop_(connection_interface &conn) {
    this->connections.erase(reinterpret_cast<std::uintptr_t>(conn.watcher.get()));
}

bool manapi::net::worker::TCP::update_limit_rate_connection(const shared_conn &sconn) {
    auto const conn_data = sconn->as<connection_interface>();

    if (conn_data->transfered >= this->config_->speed_limit_rate
        && conn_data->ev_callback) {
        conn_data->transfered = 0;
        if (conn_data->status & ev::READ)
            conn_data->watcher->read_start();

        if (conn_data->status & ev::WRITE)
            conn_data->ev_callback->operator()(sconn, ev::WRITE, nullptr, 0, nullptr);
    }
    else {
        conn_data->transfered_k += conn_data->transfered;

        if (--conn_data->speed_min_delay == 0) {
            if (conn_data->transfered_k < this->config_->speed_check_bytes) {
                this->close_connection(sconn, false);
                return true;
            }
            conn_data->transfered_k = 0;
            conn_data->speed_min_delay = static_cast<int>(this->config_->speed_check_delay);
        }
        conn_data->transfered = 0;
    }

    if (conn_data->data) {
        if (sconn->version == http::versions::HTTP_v2) {
            auto const http_v2_ctx = static_cast<http::http_v2_t *> (conn_data->data);
            for (const auto &s : *http_v2_ctx->streams) {
                this->http_v2_worker->update_limit_rate_stream(s.second);
            }
        }
    }

    return false;
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
            auto data = conn->as<connection_interface>();
            rhs = http::http_v2_work(http_v2_ctx, this->config_, &buffer, &nsize);
            switch (rhs) {
                case http::EHTTP_V2_PROTOCOL_OK: {
                    http::http_v2_on_close (http_v2_ctx);
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
                        [http_v2_ctx, sconn = s->second, conn, req = std::move(sdata->req)] (bool ok) mutable
                        -> void {
                            manapi::async::current()->etaskpool()->append_task(
                                [http_v2_ctx, ok, conn, sconn = std::move(sconn)] () -> void {
                                    auto const data = conn->as<connection_interface>();

                                    auto const wrk = dynamic_cast<TCP*>(data->worker);
                                    auto const sdata = sconn->as<http::http_v2_stream_t>();


                                    if (ok) {
                                        wrk->http_v2_worker->close_connection(sconn, true);
                                    }
                                    else {
                                        /* failed */
                                        wrk->http_v2_worker->close_connection(sconn, false);
                                    }

                                    http::http_v2_on_close_stream(http_v2_ctx, sdata->id);

                                    if (http_v2_ctx->streams->empty() &&
                                        data->status & (CONN_CLOSED|CONN_REMOVED)) {
                                        if (http::http_v2_on_close (http_v2_ctx)) {
                                            /* error */
                                        }
                                        wrk->conn_work_finish_(conn, true);
                                    }
                            });
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
        if (http::http_v2_on_close (http_v2_ctx)) {
            /* error */
        }
        if (http_v2_ctx->streams->empty())
            this->conn_work_finish_(conn, false);
    }
}

bool manapi::net::worker::TCP::is_writable(const shared_conn &conn) {
    auto const data = conn->as<connection_interface>();
    return data->top->recv_size <= this->config_->max_buffer_stack
        && data->transfered < this->config_->speed_limit_rate;
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
        manapi::async::current()->eventloop()->stop_watcher_tcp_connection(std::move(connection->watcher));
    }

    //std::cout << "CLOSE 2\n";

    auto const wrk = dynamic_cast<TCP*> (connection->worker);

    wrk->count--;
    wrk->worker_data()->count.fetch_sub(1);

    if (wrk->count < wrk->config_->max_connections) {
        // TODO: start accepting
    }

    delete connection;

    if (wrk->flags & NET_WORKER_CLOSED
        && !wrk->count
        && wrk->finish) {
        wrk->finish();
    }
}

void manapi::net::worker::TCP::http_work_(http::http_v1_1_t *http_v1_1_ctx, const worker::shared_conn &conn, int flags, const char *buff, ssize_t size) {
    auto data = conn->as<connection_interface>();

    if (flags & ev::DISCONNECT) {
        goto err;
    }

    try {
        if (flags & ev::READ) {

            switch (http::http_v1_1_work(http_v1_1_ctx, this->config_, &buff, &size)) {
                case http::EHTTP_V1_1_PROTOCOL_OK: {
                    auto req_ptr = http_v1_1_ctx->req.get();

                    auto it_header = req_ptr->headers.find(http::HEADER.EXPECT);
                    if (it_header != req_ptr->headers.end()) {
                        ssize_t const copy = sizeof ("HTTP/1.1 100 Continue\r\n\r\n") - 1;
                        auto const rhs = sync_write_ex (conn, static_cast<const char *>("HTTP/1.1 100 Continue\r\n\r\n"),
                            copy, true, 1e5);
                        if (copy != rhs) {
                            goto err;
                        }
                    }

                    it_header = req_ptr->headers.find(http::HEADER.TRANSFER_ENCODING);
                    if (it_header != req_ptr->headers.end()) {
                        auto const values = http::parse_header_value(it_header->second);
                        for (const auto &v : values) {
                            if (manapi::string::equals(v.value, "chunked", 0b11)) {
                                data->status |= CONN_HTTP_1_1_CHUNKED;
                                if (!data->data) {
                                    data->data = new connection_data_t;
                                }
                                static_cast<connection_data_t *> (data->data)
                                    ->chunked_ctx = std::make_unique<http::http_v1_1_chunked_t>();

                                continue;
                            }

                            goto err;
                        }
                    }

                    data->status |= CONN_LIMIT_RATE;
                    auto cdata = std::make_unique<http::internal::handle_data_t>(conn, this->self_.lock(), req_ptr, std::make_unique<http::internal::cont_callback_cb_t>(
                        [this, conn, req = std::move(http_v1_1_ctx->req)] (bool ok)
                        -> void {
                            conn_work_finish_ (conn, ok);
                    }));

                    this->event_on(conn, std::unique_ptr<worker_watcher_cb>(nullptr));
                    this->event_flags(conn, 0);

                    connection_io_send(&data->top->recv, buff, size,
                        this->bufferpool().get(), this->config_->buffer_size, &data->top->recv_size, 1e5);

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
                            auto http2_ctx = std::make_unique<http::http_v2_t>(http::http_v2_t{});
                            conn->version = http::versions::HTTP_v2;
                            http2_ctx->conn = conn;
                            http2_ctx->worker = this;
                            http2_ctx->http_v2_worker = this->http_v2_worker;

                            data->status |= CONN_LIMIT_RATE;
                            data->data = http2_ctx.get();

                            this->event_on(conn, std::make_unique<worker_watcher_cb>(
                                [this, ctx = std::move(http2_ctx)]
                                (const shared_conn & conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p)
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
                        this->bufferpool().get(), this->config_->buffer_size, &data->top->recv_size, 1e5);

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
    auto http_v1_1_ctx = std::make_unique<http::http_v1_1_t>(http::http_v1_1_t{});

    this->event_on(conn,
        std::make_unique<worker_watcher_cb>([this, http_v1_1_ctx = std::move(http_v1_1_ctx)]
        (const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) mutable
        -> void {
            this->http_work_ (http_v1_1_ctx.get(), conn, flags, buffer, nsize);
    }));

    this->event_flags(conn, ev::READ);
}

void manapi::net::worker::TCP::conn_work_finish_(worker::shared_conn conn, bool ok, ibuffpool_t buffer) {
    auto const data = conn->as<connection_interface>();

    data->data = nullptr;

    if (data->status & CONN_LIMIT_RATE)
        data->status ^= CONN_LIMIT_RATE;

    if (data->status & CONN_HTTP_1_1_CHUNKED) {
        data->status ^= CONN_HTTP_1_1_CHUNKED;
        delete static_cast<connection_data_t *>(std::exchange(data->data, nullptr));
    }

    //std::cout << "CLOSE\n";

    if (ok) {
        if (this->config_->keep_alive) {
            this->event_on(conn,
                std::make_unique<manapi::net::worker::worker_watcher_cb>(
                    [this]
                (const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) mutable
                    -> void {
                        if (flags & ev::DISCONNECT) {
                            this->close_connection(conn, false);
                            return;
                        }

                        if (flags & ev::READ) {
                            auto const data = conn->as<connection_interface>();
                            if (data->status & CONN_KEEP_ALIVE) {
                                data->status ^= CONN_KEEP_ALIVE;

                                if (data->t) {
                                    data->t.stop();
                                    data->t.clear();

                                    data->t = nullptr;
                                }
                            }

                            auto const w = this;
                            w->onaccept_event_(conn);
                            w->feed_event(conn, flags, buffer, nsize, p);
                        }
                }));
            this->event_flags(conn, ev::READ);


            if (buffer != nullptr && !buffer->empty()) {
                this->onrecv(data->watcher, conn, std::move(buffer));
            }
            else {
                data->worker->close_connection(std::move(conn), true);
            }
        }
        else {
            this->event_on(conn, nullptr);
            this->event_flags(conn, 0);
            data->worker->close_connection(std::move(conn), true);
        }
    }
    else {
        /* failed */
        this->event_on(conn, nullptr);
        this->event_flags(conn, 0);
        data->worker->close_connection(std::move(conn), false);
    }
}
