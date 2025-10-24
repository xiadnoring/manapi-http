#include "worker/ManapiTcp.hpp"
#include "ManapiParams.hpp"

#include <iostream>
#include <csignal>
#include <utility>
#include <vector>
#include <memory.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <memory>
#include <set>
#include <future>

#include "ManapiString.hpp"
#include "ManapiDns.hpp"
#include "ManapiTimerPool.hpp"
#include "http/ManapiBaseHttp.hpp"
#include "http/ManapiHttpRequest.hpp"
#include "http/ManapiHttpResponse.hpp"
#include "std/ManapiAsyncSocket.hpp"
#include "worker/ManapiBaseUtils.hpp"
#include "../include/http/ManapiHttp2.hpp"
#include "../include/ManapiUtils.hpp"

// TLS: 454978.10 in sec | 348111.84 in sec (STUPID METHOD)
// TCP: 661876.15 in sec | 560063.69 in sec (STUPID METHOD)

manapi::net::worker::TCP::TCP(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config) : interface_worker (std::move(site), std::move(wdata), config) {
    this->local = nullptr;
    this->finish = nullptr;
    this->flags_ = 0;
    this->count = 0;
}

manapi::net::worker::TCP::~TCP() {
    if (this->local) {
        ev::getaddrinfo::free(this->local);
    }

    if (this->limit_rate_timer) {
        this->limit_rate_timer.stop();
        this->limit_rate_timer.clear();
        this->limit_rate_timer = nullptr;
    }
}

manapi::future<manapi::error::status> manapi::net::worker::TCP::init(std::size_t deep) {
    try {
        addrinfo hints = {
            .ai_family      = PF_UNSPEC,
            .ai_socktype    = SOCK_STREAM,
            .ai_protocol    = IPPROTO_TCP
        };

        auto &address = this->config_->address;
        auto &port = this->config_->port;

        int rhs = co_await dns::getaddrinfo(address.data(), port.data(), &hints, &this->local);
        if (rhs)
            co_return error::status_internal("tcp:failed to resolve host");

        this->config_->server_len=static_cast<socklen_t>(this->local->ai_addrlen);
        memcpy (&this->config_->server_addr,this->local->ai_addr, this->local->ai_addrlen);

        manapi_log_trace(debug::LOG_TRACE_HIGH, "TCP PORT USED: %.*s. %.*s:%.*s", port.size(), port.data(),
            address.size(), address.data(), port.size(), port.data());

        /* every 1 second */
        auto timer_status = manapi::async::current()->timerpool()->append_interval_sync(1000,manapi::TIMER_IMPORTANT,
            [this] (const manapi::timer& t) -> void { this->update_limit_rate(); });

        if (!timer_status.ok())
            co_return timer_status.err();

        this->limit_rate_timer = timer_status.unwrap();

        auto wres = manapi::async::current()->eventloop()->create_watcher_tcp_accept(
            [this] (const std::shared_ptr<ev::tcp> & w, int status)
            -> void {
                this->onaccept(w, status);
            });

        if (!wres)
            co_return wres.err();

        this->watcher_accept_ = wres.unwrap();

        memset(&this->sockaddrin, '\0', sizeof (sockaddr));

        if (this->local->ai_family == ev::IPv4) {
            rhs = this->watcher_accept_->ip4_addr(this->config_->address.data(), std::stoi(this->config_->port), reinterpret_cast<sockaddr_in *>(&this->sockaddrin));
            if (rhs) {
                manapi_log_error("tcp:couldn't set ipv4 addr due to result: %s", ev::strerror(rhs));
                co_return error::status_internal("tcp:ip4_addr failed");
            }
        }
        else if (this->local->ai_family == ev::IPv6) {
            rhs = this->watcher_accept_->ip6_addr(this->config_->address.data(), std::stoi(this->config_->port), reinterpret_cast<sockaddr_in6 *>(&this->sockaddrin));
            if (rhs) {
                manapi_log_error("tcp:couldn't set ipv6 addr due to result: %s", ev::strerror(rhs));
                co_return error::status_internal("tcp:ip6_addr failed");
            }
        }

        rhs = this->watcher_accept_->nodelay(this->config_->tcp_no_delay);
        if (rhs) {
            manapi_log_error("tcp:couldn't set nodelay due to result: %s", ev::strerror(rhs));
            co_return error::status_internal("tcp:nodelay failed");
        }

        rhs = this->watcher_accept_->simultaneous_accepts(this->config_->simultaneous_accepts);
        if (rhs) {
            manapi_log_error ("tcp:couldn't set simultaneous_accepts due to result:%s",ev::strerror(rhs));
            co_return error::status_internal("tcp:simultaneous_accepts failed");
        }

        rhs = this->watcher_accept_->keepalive(!!this->config_->keep_alive, this->config_->keep_alive);
        if (rhs) {
            manapi_log_error("tcp:couldn't set keep-alive due to result:%s", rhs);
            co_return error::status_internal("tcp:keepalive failed");
        }

        {
            int bind_flags = 0;
#if defined(__unix__) && !defined(__APPLE__)
            bind_flags |= ev::TCP_REUSEPORT;
#endif
            rhs = this->watcher_accept_->s_bind(reinterpret_cast<sockaddr *> (&this->sockaddrin), bind_flags);
            if (rhs) {
                manapi_log_error("tcp:couldn't bind socket due to result:%s", ev::strerror(rhs));
                co_return error::status_internal("tcp:s_bind failed");
            }
        }

        rhs = this->watcher_accept_->listen(this->config_->tcp_backlog);
        if (rhs) {
            manapi_log_error("tcp:couldn't listen socket due to result:%s", ev::strerror(rhs));
            co_return error::status_internal("tcp:listen failed");
        }


        co_return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "tcp: init failed", e.what());
    }

    co_return error::status_internal("tcp: init failed");
}

void manapi::net::worker::TCP::waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXCEPT {
    prepared::waiting(conn, state);
}

void manapi::net::worker::TCP::onaccept(const std::shared_ptr<ev::tcp> &watcher, int status) MANAPIHTTP_NOEXCEPT {
    if (status) {
        return;
    }

    shared_conn connection;
    // if (this->config_->max_connections() <= this->connections.size()) {
    //     return;
    // }

    connection = this->accept(watcher);

    if (!connection) {
        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s:%s failed", "tcp", "new_connection");
        return;
    }

    if (this->onaccept_event_(connection))
        goto err;

    return;

    err: if (connection) {
        this->close_connection(connection, CLOSE_CONN_ERR);
    }
}

void manapi::net::worker::TCP::onrecv(const std::shared_ptr<ev::tcp> &watcher, const worker::shared_conn &conn, ibuffpool_t buffer) MANAPIHTTP_NOEXCEPT {
    auto const connection = conn->as<tcp_connection_t>();
    auto const size = static_cast<int>(buffer.size());

    manapi_log_trace_hard("tcp:recv conn=%p data=%p size=%zu", conn.get(), buffer.data(), buffer.size());

    connection->transfered += size;
    if (connection->transfered >= this->config_->speed_limit_rate) {
        this->read_stop_(connection);
    }

    if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
        this->global_.custom_read_cb(conn, ev::READ, buffer.data(), size, &buffer, &this->global_, this);
    else {
        if (call_user_callback(&connection->ev_callback, conn, ev::READ, buffer.data(), size, &buffer))
            this->close_connection(conn, CLOSE_CONN_ERR);
    }
}

std::shared_ptr<manapi::net::worker::TCP> manapi::net::worker::TCP::create(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config) {
    auto worker = std::make_shared<worker::TCP>(std::move(site), std::move(wdata), config);
    return std::move(worker);
}

void on_client_close_ (uv_handle_t *handle) {
    delete static_cast<manapi::ev::tcp *> (handle->data);
}

manapi::net::worker::shared_conn manapi::net::worker::TCP::accept (const ev::shared_tcp &w, shared_conn (*init_cb) (void *user_data), void *user_data) MANAPIHTTP_NOEXCEPT {
    /**
     * Receving using 65k buffers,
     * but if we copy that buffer we
     * should copy to 4k buffers
     *
     * I mean only one 65k buffer
     * can be exists by thread
     */
    if (this->count > this->config_->max_connections + this->config_->max_connections) {
        std::unique_ptr<ev::tcp> client (new (std::nothrow) ev::tcp{});
        if (!client)
            return nullptr;
        auto const ptr = client.get();
        auto const loop = manapi::async::current()->eventloop()->loop();
        if (!loop)
            return nullptr;
        ptr->bind(loop);
        if (auto const rhs = ptr->accept(w.get())) {
            manapi_log_error("%s due to %d", "accept() failed", rhs);
            return nullptr;
        }
        ptr->data(client.release());
        if (auto const rhs = ptr->close_reset(on_client_close_)) {
            manapi_log_error("%s due to %d", "close_reset() failed", rhs);
            return nullptr;
        }

        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s due to %s", "tcp:%p conn closed", "limits");

        return nullptr;
    }

    std::shared_ptr<worker::connection> connection;
    std::array<char, 17> arr;
    arr.fill('\0');

    try {
        connection = init_cb(user_data);

        if (!connection)
            return connection;

        auto wres = manapi::async::current()->eventloop()->create_watcher_tcp_connection(
            [this, weak = std::weak_ptr(connection)] (const std::shared_ptr<ev::tcp> &w, ssize_t nread, const uv_buf_t *buf)
            -> void {
                try {
                    bytebuffer object;
                    auto const connection = weak.lock();

                    if (buf->base)
                        object = this->bufferpool().buffer(buf->base, buf->len);

                    if (!nread || !connection)
                        return;

                    if (nread < 0) {
                        if (nread == ev::ERR_AGAIN)
                            return;

                        /* maybe EOF */
                        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "EOF was received %p conn", connection->as<tcp_connection_t>());
                        this->close_connection(connection, CLOSE_CONN_EOR);
                        return;
                    }


                    auto resize_res = object.resize(nread);
                    assert(resize_res.ok());

                    this->onrecv(w, connection, std::move(object));
                }
                catch (std::exception const &e) {
                    manapi::async::current()->logger()->error(manapi::logger::default_service,
                        manapi::ERR_INTERNAL, "tcp: onrecv(...) unexpected error: {}", e.what());
                }
        }, [this] (const std::shared_ptr<ev::tcp> &, size_t suggested_size, ev::buff_t *buff) MANAPIHTTP_NOEXCEPT -> void {
            auto res = this->bufferpool().buffer(suggested_size);
            if (res.ok()) {
                auto buffer = res.unwrap();
                buff->len = static_cast<decltype(buff->len)>(buffer.size());
                buff->base = static_cast<char *>(buffer.release());
            }
        });

        if (!wres) {
            goto err;
        }

        auto client = wres.unwrap();

        if (auto rhs = client->accept(w.get())) {
            manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s due to %s", "tcp:accept", ev::strerror(rhs));
            manapi::async::current()->eventloop()->stop_watcher(std::move(client));
            return nullptr;
        }


        if (auto rhs = client->keepalive(!!this->config_->keep_alive, this->config_->keep_alive)) {
            manapi_log_error("%s due to %s", "tcp:couldn't set keep-alive", ev::strerror(rhs));
            manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set keep-alive due to result - {}", rhs);
            manapi::async::current()->eventloop()->stop_watcher(std::move(client));
            return nullptr;
        }

        auto conn = connection->as<tcp_connection_t>();

        conn->worker = this;
        conn->watcher = std::move(client);

        if (this->count >= this->config_->max_connections) {
            connection->wrk.flags |= WRK_INTERFACE_CONN_RETRY;
        }

        this->count++;
        this->worker_data_->as<http::server_ctx::worker_data_t>()->count.fetch_add(1);

        connection->ipdata = std::make_unique<decltype(connection)::element_type::ipdata_t>();
        connection->ipdata->len = 0;

        int addrlen = sizeof (connection->ipdata->client.data);
        if (auto rhs = conn->watcher->getpeername(reinterpret_cast <sockaddr *>(connection->ipdata->client.data), &addrlen)) {
            manapi_log_error("getpeername() failed: %d", rhs);
            return nullptr;
        }
        connection->ipdata->len = addrlen;

        conn->top = std::make_unique<connection_io>();

        auto const sn = reinterpret_cast <sockaddr *>(connection->ipdata->client.data);
        arr[0] = static_cast<char>(http::version_ip_by_addr (sn));
        http::ip_by_addr(sn, arr.data() + 1);

        auto findex = std::string_view(arr.data(), arr.size());
        auto it = this->ips.find(findex);
        if (it == this->ips.end())
            it = this->ips.insert({std::string(findex), conn_by_port{}}).first;

        if (it->second.size() >= this->config_->max_connections_by_ip) {
            if (it->second.size() > this->config_->max_connections_by_ip * 2)
                return nullptr;

            connection->wrk.flags |= WRK_INTERFACE_CONN_RETRY;
        }

        auto res = it->second.insert({reinterpret_cast<uintptr_t>(connection.get()),
            connection});
        assert((res.second));
    }
    catch (std::exception const &e) {
        manapi_log_error("tcp accept: failed due to %s", e.what());

        goto err;
    }

    return std::move(connection);
    err:
    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s:%s failed", "tcp", "accept");
    if (connection) {
        auto it = this->ips.find(std::string_view(arr.data(), arr.size()));
        if (it != this->ips.end())
            it->second.erase(reinterpret_cast<uintptr_t>(connection.get()));
    }

    return nullptr;
}

manapi::net::worker::shared_conn manapi::net::worker::TCP::accept(const ev::shared_tcp &w) MANAPIHTTP_NOEXCEPT {
    return this->accept(w, connection_init_cb, this);
}

void manapi::net::worker::TCP::close_connection(shared_conn conn, int flags) MANAPIHTTP_NOEXCEPT {
    if (!conn)
        return;

    auto connection = conn->as<tcp_connection_t>();

    /* Draining timeout is enabled, thus, connection is draining */
    if ((flags & CLOSE_CONN_EOR)) {
        conn->wrk.flags |= WRK_INTERFACE_IS_DRAINING;
        connection->flags |= CONN_RECV_END;

        if (!connection->t && !(connection->flags & CONN_READ)) {
            return;
        }
    }

    this->waiting(conn, true);

    if (connection->flags & CONN_REMOVED)
        return;

    manapi_log_trace(debug::LOG_TRACE_LOW, "TCP:close_connection() %p flags=%d",connection, flags);

    conn->cancellation.cancel();

    if (  !(flags & CLOSE_CONN_FINISHED)
        || (flags & CLOSE_CONN_ERR)
        || (flags & CLOSE_CONN_EOS)
        || (flags & CLOSE_CONN_SHUTDOWN)
        || !this->config_->keep_alive
        || (conn->wrk.flags & WRK_INTERFACE_IS_DRAINING)
        || !(conn->wrk.flags & WRK_INTERFACE_TCP_KEEP_ALIVE)) {

        connection->flags |= CONN_CLOSED;

        if (flags & CLOSE_CONN_SHUTDOWN && connection->top->send_size) {
            if (!this->flush_write_(conn, true)) {
                return;
            }
        }

        if (conn->wrk.flags & WRK_INTERFACE_TCP_KEEP_ALIVE)
            conn->wrk.flags ^= WRK_INTERFACE_TCP_KEEP_ALIVE;

        connection->flags |= CONN_REMOVED;

        prepared::timer_clear(std::move(connection->t));

        prepared::event_callback_clear(conn, connection);

        this->read_stop_(connection);

        prepared::top_buffer_clear(connection);

        std::array<char, 17> arr;
        arr.fill('\0');
        auto const sn = reinterpret_cast <sockaddr *>(conn->ipdata->client.data);
        arr[0] = static_cast<char>(http::version_ip_by_addr (sn));
        http::ip_by_addr(sn, arr.data() + 1);
        auto it = this->ips.find(std::string_view(arr.data(), arr.size()));
        if (it != this->ips.end())
            it->second.erase(reinterpret_cast<uintptr_t>(conn.get()));
    }
    else {
        try {
            if (this->global_.cleanup_cb(conn.get(), &this->global_, this))
                MANAPIHTTP_LOG2("tcp this->global_.cleanup_cb failed");

            if (connection->flags & CONN_SEND_END)
                connection->flags ^= CONN_SEND_END;

            if (connection->flags & CONN_RECV_END)
                connection->flags ^= CONN_SEND_END;

            this->event_on(conn,
                [this]
                (const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) mutable
                    -> void {
                        if (flags & ev::DISCONNECT) {
                            this->close_connection(conn, CLOSE_CONN_EOS);
                            return;
                        }

                        if (flags & ev::READ) {
                            auto const data = conn->as<tcp_connection_t>();

                            prepared::timer_clear(std::move(data->t));

                            auto const w = this;

                            if (w->onreaccept_event_(conn)) {
                                this->close_connection(conn, CLOSE_CONN_ERR);
                                return;
                            }

                            assert(conn->wrk.data);
                            w->feed_event(conn, flags, buffer, nsize, p);
                        }
                });

            conn->cancellation.reset();

            prepared::timer_clear(std::move(connection->t));

            auto rhs = manapi::async::current()->timerpool()->append_interval_sync(
                this->config_->keep_alive * 1000,
                manapi::TIMER_IMPORTANT,
                [conn] (manapi::timer t) mutable
                -> void {
                dynamic_cast <TCP*>(conn->as<tcp_connection_t>()->worker)->timeout_(conn);
            });

            if (!rhs.ok())
                goto err;

            connection->t = rhs.unwrap();

            this->event_flags(conn, ev::READ);
        }
        catch (std::bad_alloc const &) {
            goto err;
        }
        catch (std::exception const &e) {
            manapi_log_error(e.what());
            goto err;
        }
    }

    return;
    err: {
        this->close_connection(conn, CLOSE_CONN_ERR);
    }
}

void manapi::net::worker::TCP::stop(std::function<void()> cb) {
    if (this->watcher_accept_) {
        try {
            manapi::async::current()->eventloop()
                ->stop_callback(this->watcher_accept_, [this, cb = std::move(cb)] (const ev::shared_tcp &w) mutable -> void {
                    this->flags_ |= WORKER_BASE_FLAG_CLOSED;
                    this->finish = std::move(cb);

                    if (!this->count) {
                        this->finish();
                    }
                });
            manapi::async::current()->eventloop()
                ->stop_watcher (std::move(this->watcher_accept_));
        }
        catch (std::exception const &e) {
            manapi_log_error("%s failed due to %s", "TCP:Stop", e.what());
            if (cb)
                cb();
        }
    }
    else
        cb();
}

void manapi::net::worker::TCP::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT {
    prepared::feed_event(this, conn, flags, buff, size, p);
}

ssize_t manapi::net::worker::TCP::sync_write_ex(const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, std::size_t maxcnt) MANAPIHTTP_NOEXCEPT {
    auto connection = conn->as<tcp_connection_t>();

    if (connection->flags & CONN_CLOSED)
        return CONN_IO_ERROR;

    ssize_t rhs;

    if (connection->top->send_size) {
        rhs = 0;
    }
    else {
        rhs = connection->watcher->try_write(buff, nbuff);

        if (rhs < 0)
            if (rhs == ev::ERR_AGAIN)
                rhs = 0;
            else {
                manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s:%s failed due to %s",
                    "TCP", "try_write", ev::strerror(static_cast<int>(rhs)));
                return -1;
            }
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
            buff->len -= static_cast<decltype(buff->len)>(skip);
        }

        for (uint32_t i = 0; i < nbuff; i++) {
            auto const prev = connection->top->send_size;
            auto const result = connection_io_send (&connection->top->send, static_cast<const char *>(buff[i].base), buff[i].len, &this->bufferpool(),
                this->config_->buffer_size, &connection->top->send_size, maxcnt);

            connection->top->cur_send_size += connection->top->send_size - prev;

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

ssize_t manapi::net::worker::TCP::sync_write(const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT {
    return prepared::sync_write(this, conn, buff, nbuff, finish);
}

manapi::net::worker::worker_watcher_cb manapi::net::worker::TCP::event_on(const shared_conn & conn, worker_watcher_cb callback) MANAPIHTTP_NOEXCEPT {
    return prepared::event_on(conn, std::move(callback));
}

int manapi::net::worker::TCP::event_flags(const shared_conn & conn, int flags) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<tcp_connection_t>();

    MANAPIHTTP_WORKER_EVENT_LOOP(data) {
        if ((status & (ev::READ|ev::DISCONNECT)) == ev::READ && data->ev_callback) {
            if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
                this->global_.flush_custom_read_cb(conn, &this->global_, this);

            this->flush_read_ (conn, data);

            if (status & CONN_RECV_END) {
                if (manapi::net::worker::TCP::call_user_callback(&data->ev_callback, conn, CONN_RECV_END, nullptr, 0, nullptr))
                    this->close_connection(conn, CLOSE_CONN_ERR);
            }

        }

        MANAPIHTTP_WORKER_EVENT_BREAK(data)
    }

    if (data->flags & ev::READ)
        this->read_start_(data);
    else
        this->read_stop_(data);

    return prev;
}

int manapi::net::worker::TCP::event_flags(const shared_conn & conn) MANAPIHTTP_NOEXCEPT {
    return prepared::event_flags(conn);
}

std::size_t manapi::net::worker::TCP::recv_count(const shared_conn &conn) const MANAPIHTTP_NOEXCEPT {
    return prepared::recv_count(conn);
}

manapi::bytebuffer manapi::net::worker::TCP::recv_first_buffer(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    return prepared::recv_first_buffer(conn);
}

void manapi::net::worker::TCP::read_start_(tcp_connection_t *data) MANAPIHTTP_NOEXCEPT {
    if (!data->watcher || data->watcher->is_active() || data->transfered > this->config_->speed_limit_rate)
        return;
    manapi_log_trace(debug::LOG_TRACE_LOW, "TCP:%p read_start()", data);
    auto res = data->watcher->read_start();
    if (res)
        manapi_log_error("%s failed due to %s", "tcp:read_start", ev::strerror(res));
}

void manapi::net::worker::TCP::read_stop_(tcp_connection_t *data) MANAPIHTTP_NOEXCEPT {
    if (!data->watcher || !data->watcher->is_active())
        return;
    manapi_log_trace(debug::LOG_TRACE_LOW, "TCP:%p read_stop()", data);
    auto res = data->watcher->read_stop();
    if (res)
        manapi_log_error("%s failed due to %s", "tcp:read_stop", ev::strerror(res));
}

int manapi::net::worker::TCP::flush_write_(const worker::shared_conn &connection, bool flush) MANAPIHTTP_NOEXCEPT {
    auto conn = connection->as<tcp_connection_t>();

    try {
        if (conn->top && conn->top->cur_send_size) {
            //std::cout << "flush " << flush << " "<<(bool)conn->top->cur_send_size << " " << (bool)conn->top->send.deque << "\n";
            while (conn->top->cur_send_size && ((conn->top->cur_send_size >= this->config_->max_merge_buffer_stack)
                //|| ((conn->top->cur_send_size == this->config_->max_merge_buffer_stack) && (conn->top->send.last_deque->buffer.size() == conn->top->send.deque_cursor))
                || (flush))) {
#ifdef _MSC_VER
                ev::buff_t *s = static_cast<ev::buff_t*>(alloca(sizeof (ev::buff_t) * conn->top->cur_send_size));
#else
                ev::buff_t s[conn->top->cur_send_size];
#endif

                std::unique_ptr<buffer_deque> sent = std::move(conn->top->send.deque);
                auto current = sent.get();
                ssize_t request = 0;

                for (int i = 0; i < conn->top->cur_send_size; i++) {
                    auto &object = current->buffer;

                    if (current == conn->top->send.last_deque) {
                        conn->top->send.last_deque = nullptr;
                        auto resize_res = object.resize(conn->top->send.deque_cursor);
                        assert(resize_res.ok());
                        conn->top->send.deque_cursor = 0;
                    }

                    if (conn->top->send.deque_current) {
                        object.shift_add(conn->top->send.deque_current);
                        conn->top->send.deque_current = 0;
                    }

                    s[i].base = object.data();
                    s[i].len = static_cast<decltype(s[i].len)>(object.size());

                    request += static_cast<ssize_t>(s[i].len);

                    if (i + 1 != conn->top->cur_send_size)
                        current = current->next.get();
                }

                if (current && current->next)
                    conn->top->send.deque = std::move(current->next);

                ssize_t rhs;
                if (conn->top->cur_send_size == conn->top->send_size)
                    rhs = conn->watcher->try_write(s, conn->top->cur_send_size);
                else
                    rhs = 0;

                if (request == rhs) {
                    conn->top->send_size -= conn->top->cur_send_size;
                    conn->top->cur_send_size = 0;
                }
                else {
                    if (rhs < 0) {
                        if (rhs == ev::ERR_AGAIN)
                            rhs = 0;
                        else {
                            manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s:%s failed due to %s",
                                "TCP", "try_write", ev::strerror(static_cast<int>(rhs)));
                            conn->top->send_size -= conn->top->cur_send_size;
                            conn->top->cur_send_size = 0;
                            return CONN_IO_ERROR;
                        }
                    }


                    int cursor = 0;
                    while (cursor != conn->top->cur_send_size
                        && rhs >= s[cursor].len) {
                        rhs -= static_cast<ssize_t>(s[cursor].len);
                        sent = std::move(sent->next);
                        cursor++;
                        }

                    conn->top->cur_send_size -= cursor;
                    conn->top->send_size -= cursor;

                    if (conn->top->cur_send_size
                        && (conn->top->cur_send_size >= this->config_->max_merge_buffer_stack || flush)) {

                        std::unique_ptr<ev::buff_t, ev::buffer_deleter> sn (
                            new (std::nothrow) ev::buff_t[conn->top->cur_send_size]);

                        if (sn) {
                            auto const buffptr = sn.get();

                            for (std::size_t i = 0; i < conn->top->cur_send_size; i++) {
                                buffptr[i] = s[i + cursor];
                            }

                            if (rhs && sent) {
                                sent->buffer.shift_add(rhs);
                                buffptr->base += rhs;
                                buffptr->len -= static_cast<decltype(buffptr->len)>(rhs);
                            }


                            auto nbuff = conn->top->cur_send_size;
                            auto w = manapi::async::current()->eventloop()
                                ->create_watcher_write(conn->watcher.get(), [connection, nbuff, b = std::move(sent), s = std::move(sn)]
                                    (const std::shared_ptr<ev::write> &w, int status)
                                    mutable -> void {
                                    auto conn = connection->as<tcp_connection_t>();

                                    conn->top->send_size -= nbuff;
                                    s.reset();
                                    b.reset();

                                    if (status) {
                                        /* error */
                                        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "write failed msg=%s %p conn",
                                            ev::strerror(status), connection->as<tcp_connection_t>());
                                        conn->worker->close_connection(connection, CLOSE_CONN_EOS);
                                    }
                                    else {
                                        if ((conn->flags & (ev::WRITE|ev::DISCONNECT)) == ev::WRITE && conn->ev_callback) {
                                            if (base::call_user_callback(&conn->ev_callback, connection, ev::WRITE, nullptr, 0, nullptr))
                                                conn->worker->close_connection(connection, CLOSE_CONN_ERR);
                                        }

                                        if (conn->flags & ev::DISCONNECT)
                                            conn->worker->close_connection(connection, CLOSE_CONN_SHUTDOWN);
                                    }

                                }, buffptr, nbuff /* nbuf */);

                            conn->top->cur_send_size = 0;
                        }

                        continue;
                    }

                    if (rhs && sent) {
                        sent->buffer.shift_add(rhs);
                    }

                    if (sent) {
                        assert (current);
                        if (conn->top->send.last_deque) {
                            current->next = std::move(conn->top->send.deque);
                        }
                        else {
                            conn->top->send.last_deque = current;
                            conn->top->send.deque_cursor = static_cast<int>(conn->top->send.last_deque->buffer.size());
                            auto resize_res = conn->top->send.last_deque->buffer.resize(current->buffer.realsize() - current->buffer.shift());
                            assert(resize_res.ok());
                        }

                        conn->top->send.deque = std::move(sent);
                    }

                }
                }
        }

        return CONN_IO_OK;
    }
    catch (std::bad_alloc const &) {
        return CONN_IO_ERROR;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "flush_write_", e.what());
    }
    return CONN_IO_ERROR;
}

void manapi::net::worker::TCP::flush_read_(const shared_conn &conn, tcp_connection_t *data) MANAPIHTTP_NOEXCEPT {
    prepared::flush_read_(this, conn, data);

    if (data->flags & ev::READ
            && !(data->flags & (CONN_CLOSED|CONN_REMOVED))) {
        this->read_start_(data);
    }
}

void manapi::net::worker::TCP::update_limit_rate() MANAPIHTTP_NOEXCEPT {
    /* in the event loop */
    for (auto it = this->ips.begin(); it != this->ips.end(); ) {
        if (it->second.empty()) {
            it = this->ips.erase(it);
            continue;
        }

        for (auto nit = it->second.begin(); nit != it->second.end(); ) {
            auto conn = nit->second;
            auto next = std::next(nit);
            //auto s = conn->as<TCP::tcp_connection_t>();
            this->update_limit_rate_connection(conn);
            nit = next;
        }

        ++it;
    }
}

void manapi::net::worker::TCP::timeout_(shared_conn conn) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<tcp_connection_t>();

    prepared::timer_clear(std::move(data->t));

    if (conn->wrk.flags & WRK_INTERFACE_TCP_KEEP_ALIVE)
        conn->wrk.flags ^= WRK_INTERFACE_TCP_KEEP_ALIVE;

    this->close_connection(conn, CLOSE_CONN_SHUTDOWN);
}

void manapi::net::worker::TCP::update_limit_rate_connection(const shared_conn &sconn) MANAPIHTTP_NOEXCEPT {
    prepared::update_limit_rate_connection(sconn, this, this->config_, &this->global_);
}

bool manapi::net::worker::TCP::is_writable(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<tcp_connection_t>();
    return prepared::is_writable(this->config_, conn, data);
}

manapi::net::worker::shared_conn manapi::net::worker::TCP::connection_init_cb(void *user_data) MANAPIHTTP_NOEXCEPT {
    try {
        auto p = std::make_unique<tcp_connection_t>();

        return std::shared_ptr<worker::connection> (new worker::connection{p.release()},
            connection_interface_eraser);
    }
    catch (std::exception const &) {
        return nullptr;
    }
}

void manapi::net::worker::TCP::connection_interface_eraser(worker::connection *ptr) MANAPIHTTP_NOEXCEPT {
    if (!ptr)
        return;

    auto uptr = std::unique_ptr<worker::connection> (ptr);
    auto connection = std::unique_ptr<tcp_connection_t> (uptr->as<tcp_connection_t>());

    if (connection->watcher) {
        if (connection->watcher->is_active()) {
            manapi_log_trace(debug::LOG_TRACE_LOW, "TCP:%p read_stop()", connection.get());
            connection->watcher->read_stop();
        }
        manapi::async::current()->eventloop()->stop_watcher(std::move(connection->watcher));
    }

    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "TCP:Free TCP %p conn", connection.get());

    auto const wrk = dynamic_cast<TCP*> (connection->worker);
    if (wrk) {
        if (wrk->global_.cleanup_cb(ptr, &wrk->global_, wrk))
            MANAPIHTTP_LOG2("tcp this->global_.cleanup_cb failed");

        wrk->count--;
        wrk->worker_data()->as<http::server_ctx::worker_data_t>()->count.fetch_sub(1);

        if (wrk->count < wrk->config_->max_connections) {
            // TODO: start accepting
        }

        if (wrk->flags_ & WORKER_BASE_FLAG_CLOSED
            && !wrk->count
            && wrk->finish)
            wrk->finish();
    }
}

int manapi::net::worker::TCP::onaccept_event_(const worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    return onreaccept_event_(conn);
}

int manapi::net::worker::TCP::onreaccept_event_(const worker::shared_conn &conn) noexcept(true) {
    if (this->global_.init_cb(conn, &this->global_, this))
            return -1;

    return this->onaccept_bind_(conn);
}

int manapi::net::worker::TCP::onaccept_bind_(const worker::shared_conn &conn) noexcept(true) {
    this->waiting(conn, true);

    try {
        this->event_on(conn,
            [this]
            (const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) mutable
            -> void {
                auto const w = this;
                if (w->global_.accept_cb (conn, flags, buffer, nsize, p,
                    &w->global_, w)) {
                    w->close_connection(conn, CLOSE_CONN_ERR);
                }
        });
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "onaccept_event_", e.what());
        return -1;
    }

    this->event_flags(conn, ev::READ);

    return 0;
}
