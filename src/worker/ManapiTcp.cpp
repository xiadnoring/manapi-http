#include "worker/ManapiTcp.hpp"
#include "ManapiParams.hpp"
#include "../include/ManapiUtils.hpp"

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

#include "http/ManapiHttp2.hpp"

#include "../include/ManapiUtils.hpp"
#include "async/ManapiAsyncSocket.hpp"
#include "http/ManapiBaseHttp.hpp"

#include "ManapiHttpRequest.hpp"
#include "ManapiHttpResponse.hpp"
#include "ManapiString.hpp"

// TLS: 454978.10 in sec | 348111.84 in sec (STUPID METHOD)
// TCP: 661876.15 in sec | 560063.69 in sec (STUPID METHOD)

manapi::net::worker::TCP::TCP(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config) : interface_worker (std::move(site), std::move(wdata), config) {
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

manapi::error::status manapi::net::worker::TCP::init(std::size_t deep) {
    try {
        addrinfo hints = {
            .ai_family      = PF_UNSPEC,
            .ai_socktype    = SOCK_STREAM,
            .ai_protocol    = IPPROTO_TCP
        };

        auto &address = this->config_->address;
        auto &port = this->config_->port;

        if (getaddrinfo(address.data(), port.data(), &hints, &this->local) != 0)
            return error::status_internal("tcp:failed to resolve host");

        this->config_->server_len=(this->local->ai_addrlen);
        memcpy (&this->config_->server_addr,this->local->ai_addr, this->local->ai_addrlen);

        manapi_log_trace(debug::LOG_TRACE_HIGH, "TCP PORT USED: %.*s. %.*s:%.*s", port.size(), port.data(),
            address.size(), address.data(), port.size(), port.data());

        /* every 1 second */
        this->limit_rate_timer = manapi::async::current()->timerpool()->append_interval_sync(1000,
            [this] (const manapi::timer& t) -> void { this->update_limit_rate(); });

        this->watcher_accept_ = manapi::async::current()->eventloop()->create_watcher_tcp_accept(
            [this] (const std::shared_ptr<ev::tcp> & w, int status)
            -> void {
                this->onaccept(w, status);
            });

        memset(&this->sockaddrin, '\0', sizeof (sockaddr));

        if (this->local->ai_family == ev::IPv4) {
            if (auto rhs = this->watcher_accept_->ip4_addr(this->config_->address.data(), std::stoi(this->config_->port), reinterpret_cast<sockaddr_in *>(&this->sockaddrin))) {
                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set ipv4 addr due to result - {}", rhs);
                return error::status_internal("tcp:ip4_addr failed");
            }
        }
        else if (this->local->ai_family == ev::IPv6) {
            if (auto rhs = this->watcher_accept_->ip6_addr(this->config_->address.data(), std::stoi(this->config_->port), reinterpret_cast<sockaddr_in6 *>(&this->sockaddrin))) {
                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set ipv6 addr due to result - {}", rhs);
                return error::status_internal("tcp:ip6_addr failed");
            }
        }

        if (auto rhs = this->watcher_accept_->nodelay(this->config_->tcp_no_delay)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set nodelay due to result - {}", rhs);
            return error::status_internal("tcp:nodelay failed");
        }

        if (auto rhs = this->watcher_accept_->simultaneous_accepts(this->config_->simultaneous_accepts)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set simultaneous_accepts due to result - {}", rhs);
            return error::status_internal("tcp:simultaneous_accepts failed");
        }

        if (auto rhs = this->watcher_accept_->keepalive(!!this->config_->keep_alive, this->config_->keep_alive)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't set keep-alive due to result - {}", rhs);
            return error::status_internal("tcp:keepalive failed");
        }

        {
            int bind_flags = 0;
#if defined(__unix__) && !defined(__APPLE__)
            bind_flags |= ev::TCP_REUSEPORT;
#endif
            if (auto rhs = this->watcher_accept_->s_bind(reinterpret_cast<sockaddr *> (&this->sockaddrin), bind_flags)) {
                manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't bind socket due to result - {} {}", rhs, uv_err_name (rhs));
                return error::status_internal("tcp:s_bind failed");
            }
        }

        if (auto rhs = this->watcher_accept_->listen(this->config_->tcp_backlog)) {
            manapi::async::current()->logger()->error(logger::default_service, ERR_FAILED_PRECONDITION, "couldn't listen socket due to result - {}", rhs);
            return error::status_internal("tcp:listen failed");
        }


        return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "tcp: init failed", e.what());
    }
err:
    return error::status_internal("tcp: init failed");
}

void manapi::net::worker::TCP::waiting(const shared_conn &conn, bool state) {
    auto const d = conn->as<connection_interface>();
    if (state)
        d->status |= CONN_IO_WAITING;
    else if (d->status & CONN_IO_WAITING)
        d->status ^= CONN_IO_WAITING;
}

void manapi::net::worker::TCP::onaccept(const std::shared_ptr<ev::tcp> &watcher, int status) {
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

        if (this->onaccept_event_(connection))
            goto err;

        return;
    }
    catch (...) {
        /* error */
    }

    err: if (connection) {
        this->close_connection(connection, CLOSE_CONN_ERR);
    }
}

void manapi::net::worker::TCP::onrecv(const std::shared_ptr<ev::tcp> &watcher, const worker::shared_conn &conn, ibuffpool_t buffer) {
    auto const connection = conn->as<connection_interface>();
    auto const size = static_cast<int>(buffer.size());

    connection->transfered += size;
    if (connection->transfered >= this->config_->speed_limit_rate) {
        this->read_stop_(connection);
    }

    if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
        this->global_.custom_read_cb(conn, ev::READ, buffer.data(), size, &buffer, &this->global_, this);
    else {
        if (call_user_callback(connection->ev_callback, conn, ev::READ, buffer.data(), size, &buffer))
            this->close_connection(conn, CLOSE_CONN_ERR);
    }
}

std::shared_ptr<manapi::net::worker::TCP> manapi::net::worker::TCP::create(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::TCP>(std::move(site), std::move(wdata), config.get());
    worker->self_ = std::weak_ptr (worker);
    return std::move(worker);
}

void on_client_close_ (uv_handle_t *handle) {
    delete static_cast<manapi::ev::tcp *> (handle->data);
}

manapi::net::worker::shared_conn manapi::net::worker::TCP::accept (const ev::shared_tcp &w, std::move_only_function<shared_conn()> init) {
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
        ptr->bind(manapi::async::current()->eventloop()->loop());
        if (auto const rhs = ptr->accept(w.get())) {
            manapi_log_error("%s due to %d", "accept() failed", rhs);
            return nullptr;
        }
        ptr->data(client.release());
        if (auto const rhs = ptr->close_reset(on_client_close_)) {
            manapi_log_error("%s due to %d", "close_reset() failed", rhs);
            return nullptr;
        }
        return nullptr;
    }

    std::shared_ptr<worker::connection> connection;
    std::array<char, 17> arr{};

    try {
        connection = init();

        if (!connection)
            return connection;

        ev::shared_tcp client = manapi::async::current()->eventloop()->create_watcher_tcp_connection(
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
                        this->close_connection(connection, CLOSE_CONN_EOF);
                        return;
                    }


                    object.resize(nread);

                    this->onrecv(w, connection, std::move(object));
                }
                catch (std::exception const &e) {
                    manapi::async::current()->logger()->error(manapi::logger::default_service,
                        manapi::ERR_INTERNAL, "tcp: onrecv(...) unexpected error: {}", e.what());
                }
        }, [this] (const std::shared_ptr<ev::tcp> &, size_t suggested_size, ev::buff_t *buff) -> void {
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

        auto conn = connection->as<connection_interface>();

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
        }
        connection->ipdata->len = addrlen;

        conn->top = std::make_unique<connection_io>();

        auto const sn = reinterpret_cast <sockaddr *>(connection->ipdata->client.data);
        arr[0] = static_cast<char>(http::version_ip_by_addr (sn));
        http::ip_by_addr(sn, arr.data() + 1);

        auto &it = this->ips[arr];
        if (it.size() >= this->config_->max_connections_by_ip) {
            if (it.size() > this->config_->max_connections_by_ip * 2)
                return nullptr;

            connection->wrk.flags |= WRK_INTERFACE_CONN_RETRY;
        }

        assert((it.insert({reinterpret_cast<uintptr_t>(connection.get()),
            connection}).second));
    }
    catch (std::exception const &e) {
        manapi_log_error("tcp accept: failed due to %s", e.what());

        goto err;
    }

    return std::move(connection);
    err:
    if (connection) {
        auto it = this->ips.find(arr);
        if (it != this->ips.end())
            it->second.erase(reinterpret_cast<uintptr_t>(connection.get()));
    }

    return nullptr;
}

manapi::net::worker::shared_conn manapi::net::worker::TCP::accept(const ev::shared_tcp &w) {
    return std::move(this->accept(w,
        [this] () -> shared_conn {
            auto p = std::make_unique<connection_interface>();
            auto conn = std::shared_ptr<worker::connection> (new worker::connection{p.get()}, connection_interface_eraser);
            p.release();
            return std::move(conn);
    }));
}

void manapi::net::worker::TCP::close_connection(shared_conn conn, int flags) {
    auto connection = conn->as<connection_interface>();

    if (connection->status & CONN_REMOVED) {
        return;
    }

    manapi_log_trace(debug::LOG_TRACE_LOW, "TCP:close_connection() %p flags=%d",connection, flags);

    conn->cancellation.cancel();

    if ((flags & (CLOSE_CONN_ERR|CLOSE_CONN_EOF|CLOSE_CONN_SHUTDOWN))
        || !this->config_->keep_alive
        || !(conn->wrk.flags & WRK_INTERFACE_TCP_KEEP_ALIVE)) {

        if (conn->wrk.flags & WRK_INTERFACE_TCP_KEEP_ALIVE)
            conn->wrk.flags ^= WRK_INTERFACE_TCP_KEEP_ALIVE;

        connection->status |= CONN_REMOVED|CONN_CLOSED;

        if (connection->t) {
            connection->t.stop();
            connection->t.clear();
            connection->t = nullptr;
        }

        if (connection->ev_callback) {
            auto cb = std::move(connection->ev_callback);
            if (this->call_user_callback(cb, conn, ev::DISCONNECT, nullptr, 0, nullptr)) {
                /* pass */
            }
        }

        this->read_stop_(connection);

        if (connection->top) {
            if (connection->top->recv_size) {
                connection->top->recv.deque.reset();
                connection->top->recv.deque_current = 0;
                connection->top->recv.deque_cursor = 0;
                connection->top->recv.last_deque = nullptr;
                connection->top->recv_size = 0;
            }

            if (connection->top->send_size) {
                connection->top->send.deque.reset();
                connection->top->send.deque_current = 0;
                connection->top->send.deque_cursor = 0;
                connection->top->send.last_deque = nullptr;
                connection->top->send_size = 0;
            }
        }

        std::array<char, 17> arr{};
        auto const sn = reinterpret_cast <sockaddr *>(conn->ipdata->client.data);
        arr[0] = static_cast<char>(http::version_ip_by_addr (sn));
        http::ip_by_addr(sn, arr.data() + 1);
        auto it = this->ips.find(arr);
        if (it != this->ips.end())
            it->second.erase(reinterpret_cast<uintptr_t>(conn.get()));
    }
    else {
        if (this->global_.cleanup_cb(conn.get(), &this->global_, this))
            MANAPIHTTP_LOG2("tcp this->global_.cleanup_cb failed");

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
                        this->close_connection(conn, CLOSE_CONN_ERR);
                        return;
                    }

                    if (flags & ev::READ) {
                        auto const data = conn->as<connection_interface>();

                        if (data->t) {
                            data->t.stop();
                            data->t.clear();

                            data->t = nullptr;
                        }

                        auto const w = this;

                        if (w->onaccept_event_(conn)) {
                            this->close_connection(conn, CLOSE_CONN_ERR);
                            return;
                        }

                        w->feed_event(conn, flags, buffer, nsize, p);
                    }
            }));

        conn->cancellation.reset();

        if (connection->t) {
            connection->t.stop();
            connection->t.clear();
        }

        connection->t = manapi::async::current()->timerpool()->append_interval_sync(
            this->config_->keep_alive * 1000,
            [conn] (manapi::timer t) mutable
            -> void {
            dynamic_cast <TCP*>(conn->as<connection_interface>()->worker)->timeout_(conn);
        });

        this->event_flags(conn, ev::READ);


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
    else if (data->ev_callback) {
        if (this->call_user_callback(data->ev_callback, conn, flags, buff, size, p))
            this->close_connection(conn, CLOSE_CONN_ERR);
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
            if (rhs == ev::ERR_AGAIN)
                rhs = 0;
            else
                return -1;
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

    if (data->watcher && !(status & ev::DISCONNECT)) {
        if (status & ev::READ) {
            if (!data->watcher->is_active()) {
                this->read_start_(data);
            }
        }
        else {
            if (data->watcher->is_active()) {
                this->read_stop_(data);
            }
        }
    }

    if ((status & CONN_CLOSED|CONN_WRITE|CONN_RECV_END|CONN_READ) == (CONN_RECV_END|CONN_READ)
        && data->ev_callback) {
        if (manapi::net::worker::TCP::call_user_callback(data->ev_callback, conn, CONN_RECV_END, nullptr, 0, nullptr))
            this->close_connection(conn, CLOSE_CONN_ERR);
    }

    return prev;
}

int manapi::net::worker::TCP::event_flags(const shared_conn & conn) {
    return (conn->as<connection_interface>()->status) & CONN_MASK_GETTING;
}

std::size_t manapi::net::worker::TCP::recv_count(const shared_conn &conn) const {
    auto const s = conn->as<TCP::connection_interface>();
    return s->top->recv_size;
}

manapi::bytebuffer manapi::net::worker::TCP::recv_first_buffer(const shared_conn &conn) {
    auto const data = conn->as<TCP::connection_interface>();
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

    return std::move(object);
}

void manapi::net::worker::TCP::read_start_(connection_interface *data) {
    if (!data->watcher || data->watcher->is_active() || data->transfered > this->config_->speed_limit_rate)
        return;
    manapi_log_trace(debug::LOG_TRACE_LOW, "TCP:%p read_start()", data);
    assert(!data->watcher->read_start());
}

void manapi::net::worker::TCP::read_stop_(connection_interface *data) {
    if (!data->watcher || !data->watcher->is_active())
        return;
    manapi_log_trace(debug::LOG_TRACE_LOW, "TCP:%p read_stop()", data);
    assert(!data->watcher->read_stop());
}

int manapi::net::worker::TCP::flush_write_(const worker::shared_conn &connection, bool flush) {
    auto conn = connection->as<connection_interface>();

    if (conn->top->cur_send_size) {
        //std::cout << "flush " << flush << " "<<(bool)conn->top->cur_send_size << " " << (bool)conn->top->send.deque << "\n";
        while (conn->top->cur_send_size && ((conn->top->cur_send_size >= this->config_->max_merge_buffer_stack)
            //|| ((conn->top->cur_send_size == this->config_->max_merge_buffer_stack) && (conn->top->send.last_deque->buffer.size() == conn->top->send.deque_cursor))
            || (flush))) {

            ev::buff_t s[conn->top->cur_send_size];

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

                s[i].base = object.data();
                s[i].len = object.size();

                request += s[i].len;

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
                        new ev::buff_t[conn->top->cur_send_size]);
                    auto const buffptr = sn.get();

                    for (std::size_t i = 0; i < conn->top->cur_send_size; i++) {
                        buffptr[i] = s[i + cursor];
                    }

                    if (rhs && sent) {
                        sent->buffer.shift_add(rhs);
                        buffptr->base += rhs;
                        buffptr->len -= rhs;
                    }


                    auto w = manapi::async::current()->eventloop()
                        ->create_watcher_write(conn->watcher.get(), [connection, b = std::move(sent), s = std::move(sn)]
                            (const std::shared_ptr<ev::write> &w, int status)
                            mutable -> void {
                            auto conn = connection->as<connection_interface>();

                            conn->top->send_size -= w->custom()->nbufs;
                            s.reset();
                            b.reset();

                            if (status) {
                                /* error */
                                conn->status |= ev::DISCONNECT;
                                connection->cancellation.cancel();

                                if (conn->ev_callback) {
                                    if (conn->worker->call_user_callback(conn->ev_callback, connection, ev::DISCONNECT, nullptr, 0, nullptr))
                                        conn->worker->close_connection(connection, CLOSE_CONN_ERR);
                                }
                            }
                            else {
                                if (conn->status & ev::WRITE && conn->ev_callback) {
                                    if (conn->worker->call_user_callback(conn->ev_callback, connection, ev::WRITE, nullptr, 0, nullptr))
                                        conn->worker->close_connection(connection, CLOSE_CONN_ERR);
                                }
                            }

                            manapi::async::current()->eventloop()->stop_watcher(w);
                        }, buffptr, conn->top->cur_send_size /* nbuf */);

                    conn->top->cur_send_size = 0;
                    }
                else {
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
                            conn->top->send.deque_cursor = conn->top->send.last_deque->buffer.size();
                            conn->top->send.last_deque->buffer.resize(current->buffer.realsize() - current->buffer.shift());
                        }

                        conn->top->send.deque = std::move(sent);
                    }
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
            auto object = recv_first_buffer(conn);
            if (!object.empty()) {
                if (this->call_user_callback(data->ev_callback, conn, ev::READ, object.data(),
                    static_cast<int>(object.size()), &object))
                    this->close_connection(conn, CLOSE_CONN_ERR);
            }
        }
    }
    if (data->status & ev::READ
            && !(data->status & (CONN_CLOSED|CONN_REMOVED))) {
        this->read_start_(data);
    }
}

void manapi::net::worker::TCP::update_limit_rate() {
    /* in the event loop */
    for (auto it = this->ips.begin(); it != this->ips.end(); ) {
        if (it->second.empty()) {
            it = this->ips.erase(it);
            continue;
        }

        for (auto nit = it->second.begin(); nit != it->second.end(); ) {
            auto conn = nit->second;
            auto next = std::next(nit);
            //auto s = conn->as<TCP::connection_interface>();
            this->update_limit_rate_connection(conn);
            nit = next;
        }

        ++it;
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

    if (conn->wrk.flags & WRK_INTERFACE_TCP_KEEP_ALIVE)
        conn->wrk.flags ^= WRK_INTERFACE_TCP_KEEP_ALIVE;

    this->close_connection(conn, CLOSE_CONN_ERR);
}

void manapi::net::worker::TCP::update_limit_rate_connection(const shared_conn &sconn) {
    auto const conn_data = sconn->as<connection_interface>();

    if (conn_data->transfered >= this->config_->speed_limit_rate
        && conn_data->ev_callback) {
        conn_data->transfered = 0;
        if ((conn_data->status & (ev::READ|ev::DISCONNECT)) == ev::READ) {
            this->read_start_(conn_data);
        }

        if (conn_data->status & ev::WRITE && conn_data->ev_callback) {
            if (this->call_user_callback(conn_data->ev_callback, sconn, ev::WRITE, nullptr, 0, nullptr)) {
                this->close_connection(sconn, CLOSE_CONN_ERR);
                return;
            }
        }
    }
    else {
        conn_data->transfered_k += conn_data->transfered;

        if (--conn_data->speed_min_delay <= 0) {
            if (conn_data->status & (CONN_IO_WAITING)
                && (conn_data->transfered_k < this->config_->speed_check_bytes)) {
                this->close_connection(sconn, CLOSE_CONN_EOF);
                return;
            }
            conn_data->transfered_k = 0;
            conn_data->speed_min_delay = static_cast<int>(this->config_->speed_check_delay);
        }
        conn_data->transfered = 0;
    }

    if (sconn->wrk.flags & WRK_INTERFACE_CUSTOM_RATE_LIMIT) {
        this->global_.update_limit_rate(sconn, &this->global_, this);
    }
}

bool manapi::net::worker::TCP::is_writable(const shared_conn &conn) {
    auto const data = conn->as<connection_interface>();
    return data->top->send_size <= this->config_->max_buffer_stack
        && data->transfered < this->config_->speed_limit_rate;
}

void manapi::net::worker::TCP::connection_interface_eraser(worker::connection *ptr) {
    auto uptr = std::unique_ptr<worker::connection> (ptr);
    auto connection = std::unique_ptr<connection_interface> (uptr->as<connection_interface>());

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

        if (wrk->flags & NET_WORKER_CLOSED
            && !wrk->count
            && wrk->finish)
            wrk->finish();
    }
}

int manapi::net::worker::TCP::onaccept_event_(const worker::shared_conn &conn) {
    this->waiting(conn, true);
    if (this->global_.init_cb(conn, &this->global_, this))
        return -1;

    this->event_on(conn,
        std::make_unique<worker_watcher_cb>([]
        (const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) mutable
        -> void {
            auto const w = dynamic_cast<TCP *>(conn->as<TCP::connection_interface>()->worker);
            if (w->global_.accept_cb (conn, flags, buffer, nsize, p,
                &w->global_, w)) {
                w->close_connection(conn, CLOSE_CONN_ERR);
            }
    }));

    this->event_flags(conn, ev::READ);
    return 0;
}