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

#include "ManapiUtils.hpp"
#include "async/ManapiAsyncSocket.hpp"
#include "http/base_http.hpp"

#include "ManapiHttpRequest.hpp"
#include "ManapiHttpResponse.hpp"
#include "ManapiString.hpp"

manapi::net::worker::TCP::TCP(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, manapi::net::http::config *config) : interface_worker (std::move(site), std::move(wdata), config) {
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
            THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL, "{}", "failed to resolve host");
        }

        this->config_->server_len=(this->local->ai_addrlen);
        memcpy (&this->config_->server_addr,this->local->ai_addr, this->local->ai_addrlen);

        MANAPIHTTP_LOG("TCP PORT USED: {}. {}:{}", port, address, port);

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
                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set ipv4 addr due to result - {}", rhs);
                goto err;
            }
        }
        else if (this->local->ai_family == ev::IPv6) {
            if (auto rhs = this->watcher_accept_->ip6_addr(this->config_->address.data(), std::stoi(this->config_->port), reinterpret_cast<sockaddr_in6 *>(&this->sockaddrin))) {
                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set ipv6 addr due to result - {}", rhs);
                goto err;
            }
        }

        if (auto rhs = this->watcher_accept_->nodelay(this->config_->tcp_no_delay)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set nodelay due to result - {}", rhs);
            goto err;
        }

        if (auto rhs = this->watcher_accept_->simultaneous_accepts(this->config_->simultaneous_accepts)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set simultaneous_accepts due to result - {}", rhs);
            goto err;
        }

        if (auto rhs = this->watcher_accept_->keepalive(!!this->config_->keep_alive, this->config_->keep_alive)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set keep-alive due to result - {}", rhs);
            goto err;
        }

        {
            int bind_flags = 0;
#if defined(__unix__) && !defined(__APPLE__)
            bind_flags |= ev::TCP_REUSEPORT;
#endif
            if (auto rhs = this->watcher_accept_->s_bind(reinterpret_cast<sockaddr *> (&this->sockaddrin), bind_flags)) {
                manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't bind socket due to result - {} {}", rhs, uv_err_name (rhs));
                goto err;
            }
        }

        if (auto rhs = this->watcher_accept_->listen(this->config_->max_backlog)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't listen socket due to result - {}", rhs);
            goto err;
        }


        return;
    }
    catch (std::exception const &e) {
        manapi::async::current()->logger()->error(manapi::logger::default_service,
            ERR_FAILED_PRECONDITION, "tcp: init failed(...) due to {}", e.what());
    }
err:
    THROW_MANAPIHTTP_EXCEPTION2 (ERR_FAILED_PRECONDITION, "tcp: init(...) failed");
}

void manapi::net::worker::TCP::waiting(const shared_conn &conn, bool state) {
    auto const d = conn->as<connection_interface>();
    if (state)
        d->status |= CONN_IO_WAITING;
    else if (d->status & CONN_IO_WAITING)
        d->status ^= CONN_IO_WAITING;
}

void manapi::net::worker::TCP::configure_connection(const worker::shared_conn &connection, oncont_cb cb) {
    cb.call(true);
}

manapi::future<ssize_t> manapi::net::worker::TCP::response(const worker::shared_conn &connection, http::response *resp, bool finish) {
    const auto response = manapi::net::worker::TCP::stringify_http_info(resp, connection->version, "\r\n") + this->stringify_headers(resp, "\r\n") + "\r\n";

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
    auto const size = static_cast<int>(buffer.size());

    connection->transfered += size;
    if (connection->transfered >= this->config_->speed_limit_rate) {
        connection->watcher->read_stop();
    }

    if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
        this->global_.custom_read_cb(conn, ev::READ, buffer.data(), size, &buffer, &this->global_, this);
    else
        connection->ev_callback->operator()(conn, ev::READ, buffer.data(), size, &buffer);
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
                bytebuffer object;
                auto const connection = weak.lock();

                if (buf->base) {
                    object = this->bufferpool().buffer(buf->base, buf->len);
                }

                if (!nread) {
                    return;
                }

                if (nread < 0) {
                    /* maybe EOF */
                    this->close_connection(connection, false);
                    return;
                }


                object.resize(nread);

                this->onrecv(w, connection, std::move(object));
            }
            catch (std::exception const &e) {
                manapi::async::current()->logger()->error(manapi::logger::default_service,
                    manapi::ERR_INTERNAL, "tcp: onrecv(...) unexpected error: {}", e.what());
            }
    }, [this] (std::shared_ptr<ev::tcp> &, size_t suggested_size, ev::buff_t *buff) -> void {
        auto buffer = this->bufferpool().buffer(suggested_size);
        buff->len = buffer.size();
        buff->base = static_cast<char *>(buffer.release());
    });


    if (client->accept(w.get())) {
        manapi::async::current()->eventloop()->stop_watcher(std::move(client));
        return nullptr;
    }

    if (auto rhs = client->keepalive(!!this->config_->keep_alive, this->config_->keep_alive)) {
        manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set keep-alive due to result - {}", rhs);
        manapi::async::current()->eventloop()->stop_watcher(std::move(client));
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
            auto p = std::make_unique<connection_interface>();
            auto conn = std::shared_ptr<worker::connection> (new worker::connection{p.get()}, connection_interface_eraser);
            p.release();
            return std::move(conn);
    }));
}

void manapi::net::worker::TCP::close_connection(shared_conn conn, bool clean_disconnect) {
    auto connection = conn->as<connection_interface>();

    if (connection->status & CONN_REMOVED) {
        return;
    }

    conn->cancellation.cancel();

    if (!clean_disconnect || !this->config_->keep_alive) {

        if (connection->status & CONN_KEEP_ALIVE) {
            connection->status ^= CONN_KEEP_ALIVE;
        }

        connection->status |= CONN_REMOVED|CONN_CLOSED;

        if (connection->t) {
            connection->t.stop();
            connection->t.clear();
            connection->t = nullptr;
        }
        if (connection->ev_callback) {
            connection->ev_callback->operator()(conn, ev::DISCONNECT, nullptr, 0, nullptr);
            connection->ev_callback = nullptr;
        }

        if (connection->watcher) {
            connection->watcher->read_stop();
            manapi::async::current()->eventloop()->stop_watcher(std::move(connection->watcher));
        }

        this->connections.erase(reinterpret_cast<uintptr_t> (conn.get()));
    }
    else {
        this->global_.cleanup_cb(conn.get(), &this->global_, this);

        if (connection->status & CONN_SEND_END)
            connection->status ^= CONN_SEND_END;

        if (connection->status & CONN_RECV_END)
            connection->status ^= CONN_SEND_END;

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

        conn->cancellation.reset();

        connection->status |= CONN_KEEP_ALIVE;

        if (connection->t) {
            connection->t.stop();
            connection->t.clear();
        }

        connection->t = manapi::async::current()->timerpool()->append_interval_sync(15000,
            [conn] (manapi::timer t) mutable -> void {
            dynamic_cast <TCP*>(conn->as<connection_interface>()->worker)->timeout_(conn);
        });
    }
}

void manapi::net::worker::TCP::stop(std::function<void()> cb) {
    if (this->watcher_accept_) {
        manapi::async::current()->eventloop()
            ->stop_callback(this->watcher_accept_, [this, cb = std::move(cb)] (const ev::shared_tcp &w) -> void {
                this->flags |= NET_WORKER_CLOSED;
                this->finish = std::move(cb);

                if (!this->count) {
                    this->finish();
                }
            });
        manapi::async::current()->eventloop()
            ->stop_watcher (std::move(this->watcher_accept_));
    }
}

void manapi::net::worker::TCP::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) {
    auto const data = conn->as<connection_interface>();
    if (flags & ev::READ) {
        if (flags & CONN_TOP_READ) {
            this->feed_event_read_ (conn, data->ev_callback.get(), &data->top->recv, &data->top->recv_size, data->status, flags, buff, size, p);
            this->flush_read_(conn, data);
        }
        else {
            this->flush_read_(conn, data);
            this->feed_event_read_ (conn, data->ev_callback.get(), &data->top->recv, &data->top->recv_size, data->status, flags, buff, size, p);
        }
    }
    else {
        data->ev_callback->operator()(conn, flags, buff, size, p);
    }
}

ssize_t manapi::net::worker::TCP::sync_write_ex(const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) {
    auto connection = conn->as<connection_interface>();

    if (connection->status & CONN_CLOSED)
        return CONN_IO_ERROR;

    ssize_t rhs;

    if (connection->top->send_size) {
        rhs = 0;
    }
    else {
        rhs = connection->watcher->try_write(buff, nbuff);

        if (rhs < 0)
            rhs = 0;
        else
            connection->transfered += rhs;
    }


    if (rhs != size) {
        if (rhs) {
            auto skip = rhs;
            while (skip >= buff->len) {
                skip -= buff->len;
                buff++;
                nbuff--;
            }

            assert((nbuff > 0));

            buff->base += skip;
            buff->len -= skip;
        }

        for (uint32_t i = 0; i < nbuff; i++) {
            auto const prev = connection->top->send_size;
            auto const result = connection_io_send (&connection->top->send, static_cast<const char *>(buff[i].base), buff[i].len, &this->bufferpool(),
                this->config_->buffer_size, &connection->top->send_size, maxcnt);
            connection->top->cur_send_size += prev - connection->top->send_size;

            if (result < 0)
                return -1;

            if (!result)
                break;

            rhs += result;

            connection->transfered += rhs;
        }

        if (this->flush_write_(conn, finish))
            return -1;
    }

    return rhs;
}

ssize_t manapi::net::worker::TCP::sync_write(const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) {
    auto const connection = conn->as<connection_interface>();
    ssize_t const limit_size = this->config_->speed_limit_rate - connection->transfered;

    auto const size = buffs_cut_by_size (buff, nbuff, limit_size, finish);

    if (!size)
        return 0;

    return sync_write_ex(conn, buff, nbuff, size, finish, this->config_->max_buffer_stack);
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

    auto const prev = std::exchange(status, ((status >> 2) << 2) | (flags & CONN_MASK_UPDATE));

    if ((status & ev::READ)) {
        if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
            this->global_.flush_custom_read_cb(conn, &this->global_, this);

        flush_read_ (conn, data);

    }
    if (!(status & CONN_CLOSED|CONN_WRITE)) {
        if (status & ev::READ) {
            if (!data->watcher->is_active()) {
                assert(!data->watcher->read_start());
            }
        }
        else {
            if (data->watcher->is_active()) {
                assert(!data->watcher->read_stop());
            }
        }

        if ((status & CONN_RECV_END) && (status & CONN_READ) && data->ev_callback) {
            data->ev_callback->operator()(conn, CONN_RECV_END, nullptr, 0, nullptr);
        }
    }

    return prev;
}

int manapi::net::worker::TCP::event_flags(const shared_conn & conn) {
    return (conn->as<connection_interface>()->status) & CONN_MASK_GETTING;
}

int manapi::net::worker::TCP::flush_write_(const worker::shared_conn &connection, bool flush) {
    auto conn = connection->as<connection_interface>();

    if ((conn->top->cur_send_size > this->config_->max_merge_buffer_stack)
        || ((conn->top->cur_send_size == this->config_->max_merge_buffer_stack) && (conn->top->send.last_deque->buffer.size() == conn->top->send.deque_cursor))
        || (flush && conn->top->cur_send_size)) {
        std::unique_ptr<ev::buff_t, ev::buffer_deleter> s;
        s.reset(new ev::buff_t[conn->top->cur_send_size]);

        auto const buffptr = s.get();
        std::unique_ptr<buffer_deque> sent = std::move(conn->top->send.deque);
        auto current = sent.get();
        ssize_t request = 0;

        for (int i = 0; i < conn->top->cur_send_size; i++) {
            auto &object = current->buffer;

            if (current == conn->top->send.last_deque) {
                conn->top->send.last_deque = nullptr;
                object.resize(conn->top->send.deque_cursor);
                conn->top->send.deque_cursor = 0;
            }

            if (conn->top->send.deque_current) {
                object.shift_add(conn->top->send.deque_current);
                conn->top->send.deque_current = 0;
            }

            buffptr[i].base = object.data();
            buffptr[i].len = object.size();

            request += buffptr[i].len;

            if (i + 1 != conn->top->cur_send_size)
                current = current->next.get();
        }

        if (current && current->next)
            conn->top->send.deque = std::move(current->next);

        auto rhs = conn->watcher->try_write(buffptr, conn->top->cur_send_size);
        if (request == rhs) {
            conn->top->send_size = 0;
            conn->top->cur_send_size = 0;
        }
        else {
            if (rhs < 0)
                return CONN_IO_ERROR;


            uint32_t cursor = 0;
            while (cursor != conn->top->cur_send_size
                && rhs >= buffptr[cursor].len) {
                rhs -= static_cast<ssize_t>(buffptr[cursor].len);
                sent = std::move(sent->next);
                cursor++;
            }

            conn->top->cur_send_size -= cursor;
            conn->top->send_size -= cursor;

            if (rhs && sent) {
                sent->buffer.shift_add(rhs);
                buffptr[cursor].base += rhs;
                buffptr[cursor].len -= rhs;
            }

            if (conn->top->cur_send_size >= this->config_->max_merge_buffer_stack) {
                auto w = manapi::async::current()->eventloop()
                    ->create_watcher_write(conn->watcher.get(), [connection, b = std::move(sent), s = std::move(s)]
                        (std::shared_ptr<ev::write> &w, int status)
                        -> void {
                        auto conn = connection->as<connection_interface>();

                        conn->top->send_size -= w->custom()->nbufs;

                        if (status) {
                            /* error */
                            conn->status |= ev::DISCONNECT;
                            connection->cancellation.cancel();

                            if (conn->ev_callback)
                                conn->ev_callback->operator()(connection, ev::DISCONNECT, nullptr, 0, nullptr);
                        }
                        else {
                            if (conn->status & ev::WRITE)
                                conn->ev_callback->operator()(connection, ev::WRITE, nullptr, 0, nullptr);
                        }

                        manapi::async::current()->eventloop()->stop_watcher(w);
                    }, s.get() + cursor, conn->top->cur_send_size /* nbuf */);

                conn->top->cur_send_size = 0;
            }
            else {
                if (sent) {
                    assert (current);
                    if (conn->top->send.last_deque) {
                        current->next = std::move(conn->top->send.deque);
                    }
                    conn->top->send.deque = std::move(sent);
                    conn->top->send.last_deque = current;
                }
            }
        }
    }

    return CONN_IO_OK;
}

void manapi::net::worker::TCP::flush_read_(const shared_conn &conn, connection_interface *data) {
    if (data->top->recv_size) {
        while (data->top->recv.last_deque
            && (data->status & ev::READ)
            && data->ev_callback) {
            auto object = std::move(data->top->recv.deque->buffer);
            data->top->recv.deque = std::move(data->top->recv.deque->next);
            data->top->recv_size--;

            if (!data->top->recv.deque) {
                data->top->recv.last_deque = nullptr;
                object.resize(data->top->recv.deque_cursor);
                data->top->recv.deque_cursor = 0;
            }

            if (data->top->recv.deque_current) {
                object.shift_add(data->top->recv.deque_current);
                data->top->recv.deque_current = 0;
            }

            if (!object.empty()) {
                data->ev_callback->operator()(conn, ev::READ, object.data(),
                    static_cast<int>(object.size()), &object);
            }
        }
    }
    if (data->status & ev::READ
            && !(data->status & (CONN_CLOSED|CONN_REMOVED))
            && !data->watcher->is_active()) {
        data->watcher->read_start();
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
            if (conn_data->status & (CONN_IO_WAITING)
                && (conn_data->transfered_k < this->config_->speed_check_bytes)) {
                this->close_connection(sconn, false);
                return true;
            }
            conn_data->transfered_k = 0;
            conn_data->speed_min_delay = static_cast<int>(this->config_->speed_check_delay);
        }
        conn_data->transfered = 0;
    }

    if (sconn->wrk.flags & WRK_INTERFACE_CUSTOM_RATE_LIMIT) {
        if (this->global_.update_limit_rate(sconn, &this->global_, this)) {
            this->close_connection(sconn, false);
            return true;
        }
    }

    return false;
}

bool manapi::net::worker::TCP::is_writable(const shared_conn &conn) {
    auto const data = conn->as<connection_interface>();
    return data->top->send_size <= this->config_->max_buffer_stack
        && data->transfered < this->config_->speed_limit_rate;
}

std::string manapi::net::worker::TCP::stringify_http_info(manapi::net::http::response *res, const int &version, const std::string &delimiter) {
    return "HTTP/" + http::config::stringify_http_version(version) + ' ' + std::to_string(res->status_code()) + (version < http::versions::HTTP_v2 ? ' ' + std::string{res->status_message()} + delimiter : delimiter);
}

std::string manapi::net::worker::TCP::stringify_headers(manapi::net::http::response *res, const std::string &delimiter) {
    std::string data;

    std::size_t size = 0;
    for (const auto &header : res->headers()) {
        size += header.first.size() + (sizeof (": ") - 1) + header.second.size() + delimiter.size();
    }
    data.reserve(size);

    // add headers
    for (const auto &header: res->headers()) {
        data += header.first + ": " + header.second + delimiter;
    }

    return data;
}

void manapi::net::worker::TCP::connection_interface_eraser(worker::connection *ptr) {
    auto uptr = std::unique_ptr<worker::connection> (ptr);
    auto connection = std::unique_ptr<connection_interface> (uptr->as<connection_interface>());

    if (connection->watcher) {
        connection->watcher->read_stop();
        manapi::async::current()->eventloop()->stop_watcher(std::move(connection->watcher));
    }

    std::cout << "CLOSE TCP PEER\n";

    auto const wrk = dynamic_cast<TCP*> (connection->worker);

    wrk->count--;
    wrk->worker_data()->count.fetch_sub(1);

    if (wrk->count < wrk->config_->max_connections) {
        // TODO: start accepting
    }

    if (wrk->flags & NET_WORKER_CLOSED
        && !wrk->count
        && wrk->finish) {
        wrk->finish();
    }
}

void manapi::net::worker::TCP::onaccept_event_(const worker::shared_conn &conn) {
    this->waiting(conn, true);
    this->global_.init_cb(conn, &this->global_, this);

    this->event_on(conn,
        std::make_unique<worker_watcher_cb>([this]
        (const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) mutable
        -> void {
            this->global_.accept_cb (conn, flags, buffer, nsize, p,
                &this->global_, this);
    }));

    this->event_flags(conn, ev::READ);
}