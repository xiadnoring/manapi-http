#include "../include/worker/ManapiQuicOpenSsl.hpp"

#include <memory>

#include "ManapiString.hpp"
#include "async/ManapiEasyCancellation.hpp"
#include "services/ManapiDns.hpp"
#include "../include/worker/ManapiBaseUtils.hpp"

#ifdef MANAPIHTTP_OPENSSL_QUIC_SUPPORT

#   include <openssl/ssl.h>
#   include <openssl/quic.h>
#   include <openssl/err.h>


#   define MANAPI_AS_BIO(n) static_cast<BIO*>(n)
#   define MANAPI_AS_SSL(n) static_cast<SSL*>(n)
#   define MANAPI_AS_CTX(n_) static_cast<SSL_CTX*>(n_)
#   define MANAPI_AS_BIO_ADDR(n_) (BIO_ADDR*)(n_)

enum conn_tls_flags  {
    CONN_TLS_SHUTDOWN = 512,
    CONN_TLS_EARLY_DATA = 1024,
    CONN_TLS_EARLY_FINISHED = 2048
};

struct ssl_bio_deleter_t {
    void operator() (BIO *b) const MANAPIHTTP_NOEXPECT {
        BIO_free(b);
    }
};

struct addrinfofree_deleter {
    void operator () (addrinfo *n) const MANAPIHTTP_NOEXPECT {
        manapi::ev::getaddrinfo::free(n);
    }
};

struct manapi::net::worker::openssl_quic::quic_conn_t : connection_prepared_t {
    SSL *conn;
    manapi::timer timeout;
    openssl_quic *worker;
};

manapi::net::worker::openssl_quic::openssl_quic(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config)
    : interface_worker(std::move(site), std::move(wdata), config) {
    this->sock = 0;
    this->listener = nullptr;
    this->ctx = nullptr;
    this->count = 0;
    this->flags = 0;
}

manapi::net::worker::openssl_quic::~openssl_quic() {
    SSL_free(MANAPI_AS_SSL(this->listener));
    SSL_CTX_free(MANAPI_AS_CTX(this->ctx));
}

std::shared_ptr<manapi::net::worker::openssl_quic> manapi::net::worker::openssl_quic::create(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::openssl_quic>(std::move(site), std::move(wdata), config.get());
    worker->self_ = worker;
    return std::move(worker);
}

void ssl_dump_error_ (int err, const char *msg) {
    if (manapi::debug::log_trace_enabled < manapi::debug::LOG_TRACE_LOW)
        return;

    std::unique_ptr<BIO, ssl_bio_deleter_t> bio;
    bio.reset(BIO_new(BIO_s_mem()));
    ERR_print_errors(bio.get());
    char *buf;
    size_t len = BIO_get_mem_data(bio.get(), &buf);
    if (len && buf[len-1] == '\n')
        len--;

    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s: %s returned %d, %.*s", "openssl_quic", msg, err, len, buf);
}

manapi::error::status load_certs (SSL_CTX *ctx, std::string_view cert, std::string_view key) {
    if (SSL_CTX_use_certificate_chain_file(ctx, cert.data()) <= 0) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %.*s", "openssl_quic:couldn't load certificate file",
            cert.size(), cert.data());
        return manapi::error::status_internal("openssl_quic:couldn't load certificate file");
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key.data(), SSL_FILETYPE_PEM) <= 0) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %.*s", "openssl_quic:couldn't load key file",
            key.size(), key.data());
        return manapi::error::status_internal("openssl_quic:couldn't load key file");
    }
    if (!SSL_CTX_check_private_key(static_cast<SSL_CTX*>(ctx)))
        return manapi::error::status_failed_precondition("openssl_quic:Private key does not match the certificate public key");

    return manapi::error::status_ok();
}

static int select_alpn(SSL *ssl, const unsigned char **out,
                       unsigned char *out_len, const unsigned char *in,
                       unsigned int in_len, void *arg)
{
    auto const alpn_ossltest = static_cast<manapi::net::worker::openssl_quic *>(arg)
        ->alpn_ossltest();
    if (SSL_select_next_proto((unsigned char **)out, out_len, reinterpret_cast<const uint8_t*>(alpn_ossltest.data()),
        alpn_ossltest.size(), in, in_len) == OPENSSL_NPN_NEGOTIATED)
        return SSL_TLSEXT_ERR_OK;
    return SSL_TLSEXT_ERR_ALERT_FATAL;
}

std::string generate_alpn_ossltest (const std::vector<std::string_view> &tests) {
    std::string b;
    std::size_t s = 0;

    for (auto &test : tests) {
        if (test.size() < 256)
            s += 1 + test.size();
    }

    b.reserve(s);

    for (auto &test : tests) {
        if (test.size() < 256) {
            b.push_back(static_cast <char>(static_cast<uint8_t>(test.size())));
            b.append(test.data(), test.size());
        }
    }

    return std::move(b);
}

manapi::error::status load_params (manapi::net::worker::openssl_quic *w, SSL_CTX *ctx, manapi::json sslconfig) {
    using ci = manapi::internal::config_interface;

    auto verify_peer = ci::get_config_param<bool>(sslconfig, "verify_peer", true);
    auto alpns = ci::get_config_param<std::string>(sslconfig, "alpns", {});

    {
        /* alpns */
        auto list = manapi::string::split(alpns, ',');
        auto config_list = w->config()->alpns();

        list.insert(list.end(), config_list.begin(), config_list.end());
        list.push_back("hq-interop");

        w->alpn_ossltest(generate_alpn_ossltest(list));
    }

    SSL_CTX_set_verify(ctx, verify_peer, nullptr);
    SSL_CTX_set_alpn_select_cb(ctx, select_alpn, w);

    return manapi::error::status_ok();
}

manapi::future<manapi::error::status> manapi::net::worker::openssl_quic::init(std::size_t deep) {
    try {
        using ci = internal::config_interface;

        auto const ctx = SSL_CTX_new(OSSL_QUIC_server_method());
        if (!ctx)
            co_return error::status_internal("openssl_quic:SSL_CTX_new");

        auto &sslconfig = this->config_->ssl;

        auto const sslcert = ci::get_config_param<std::string>(sslconfig, "cert", {});
        auto const sslkey = ci::get_config_param<std::string>(sslconfig, "key", {});

        auto res = load_certs(ctx, sslcert, sslkey);
        if (!res)
            co_return std::move(res);

        res = load_params(this, ctx, std::move(sslconfig));
        if (!res)
            co_return std::move(res);

        addrinfo hints = {
            .ai_family = PF_UNSPEC,
            .ai_socktype = SOCK_DGRAM,
            .ai_protocol = IPPROTO_UDP
        };

        auto &address = this->config_->address;
        auto &port = this->config_->port;

        addrinfo *local{nullptr};

        int rhs = co_await manapi::dns::getaddrinfo(address.data(), port.data(), &hints, &local, async::timeout_cancellation(5000));
        if (rhs) {
            manapi_log_trace(debug::LOG_TRACE_HIGH, "%s failed due to %s", "dns::getaddrinfo()", ev::strerror(rhs));
            co_return error::status_internal("failed to resolve host");
        }

        std::unique_ptr<addrinfo, addrinfofree_deleter> local_st (local);

        this->config_->server_len=(local_st->ai_addrlen);
        memcpy (&this->config_->server_addr,local_st->ai_addr, local_st->ai_addrlen);

        manapi_log_trace(debug::LOG_TRACE_HIGH, "UDP PORT USED: %.*s. %.*s:%.*s",
            port.size(), port.data(), address.size(), address.data(), port.size(), port.data());

        this->sock = BIO_socket(local_st->ai_family, SOCK_DGRAM, IPPROTO_UDP, 0);

        if (this->sock <= 0)
            co_return error::status_internal("openssl_quic:BIO_socket");

        if (BIO_bind(this->sock, MANAPI_AS_BIO_ADDR(&this->config_->server_addr), 0) < 0)
            co_return error::status_internal("openssl_quic:BIO_bind");

        if (BIO_socket_nbio(this->sock, 1) <= 0)
            co_return error::status_internal("openssl_quic:BIO_socket_nbio");

        auto const listener = SSL_new_listener (ctx, 0);
        if (!listener)
            co_return error::status_internal("openssl_quic:SSL_new_listener");

        if (!SSL_set_fd(listener, this->sock))
            co_return error::status_internal("openssl_quic:SSL_set_fd");

        if (!SSL_set_blocking_mode(listener, 0))
            co_return error::status_internal("openssl_quic:SSL_set_blocking_mode");

        SSL_set_accept_state(listener);

        if (!SSL_listen(listener))
            co_return error::status_internal("openssl_quic:SSL_listen");

        this->listener = listener;

        auto loop = manapi::async::current()->eventloop()->loop();

        this->t_ = std::make_unique<ev::timer>();
        this->w_ = std::make_unique<ev::io>();

        rhs = this->t_->bind(loop);

        if (rhs) {
            this->w_.reset();
            manapi_log_trace("%s failed due to %s", "openssl_quic:uv timer bind", ev::strerror(rhs));
            co_return error::status_internal("openssl_quic:uv timer bind");
        }

        rhs = this->w_->bind(loop, this->sock);

        if (rhs) {
            this->w_.reset();
            manapi_log_trace("%s failed due to %s", "openssl_quic:uv io bind", ev::strerror(rhs));
            co_return error::status_internal("openssl_quic:uv io bind");
        }

        this->w_->data(this);
        this->t_->data(this);

        rhs = this->w_->start(ev::READ, openssl_quic::io_event_cb);

        if (rhs) {
            manapi_log_trace("%s failed due to %s", "openssl_quic:uv start", ev::strerror(rhs));
            co_return error::status_internal("openssl_quic:uv start");
        }

        co_return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "openssl_quic:init failed", e.what());
    }
    co_return error::status_internal("openssl_quic:init failed");
}

void manapi::net::worker::openssl_quic::stop(std::function<void()> cb) {
    try {
        SSL_free(MANAPI_AS_SSL(this->listener));
        SSL_CTX_free(MANAPI_AS_CTX(this->ctx));

        this->listener = nullptr;
        this->ctx = nullptr;

        if (this->w_) {
            if (auto rhs = this->w_->stop())
                manapi_log_trace("%s failed due to %s", "openssl_quic:uv stop", ev::strerror(rhs));

            this->finish = std::move(cb);
            this->w_->unbind(io_unbind_cb);
        }
        else {
            BIO_closesocket(std::exchange(this->sock, 0));

            auto func = std::move(cb);
            func ();
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "openssl_quic", "stop", e.what());
    }

    if (cb)
        cb();
}

void manapi::net::worker::openssl_quic::close_connection(shared_conn conn, int flags) {
    if (!conn)
        return;

    auto s = conn->as<quic_conn_t>();

    if (s->flags & CONN_REMOVED)
        return;

    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "openssl_quic:close_connection() %p flags=%d", s->conn, flags);

    prepared::timer_clear(std::move(s->timeout));

    if (flags != CLOSE_CONN_EOF) {

        s->timeout = manapi::async::current()->timerpool()->append_timer_sync(
            2000, [conn] (const manapi::timer &t) mutable -> void {
            auto const s = conn->as<quic_conn_t>();
            s->worker->close_connection(std::move(conn), CLOSE_CONN_EOF);
        });

        s->flags |= CONN_TLS_SHUTDOWN;

        conn_processing(s->conn);

        openssl_quic::io_event_cb(this->w_->custom(), 0, 0);

        return;
    }

    if (conn->wrk.flags & WRK_INTERFACE_TCP_KEEP_ALIVE)
        conn->wrk.flags ^= WRK_INTERFACE_TCP_KEEP_ALIVE;

    s->flags |= CONN_REMOVED|CONN_CLOSED;

    prepared::event_callback_clear(conn, s);
    prepared::top_buffer_clear(s);

    auto it = this->conns_.find(reinterpret_cast<uintptr_t>(s->conn));
    assert(it != this->conns_.end());
    this->conns_.erase(it);
}

int manapi::net::worker::openssl_quic::event_flags(const shared_conn &conn) {
    return prepared::event_flags(conn);
}

int manapi::net::worker::openssl_quic::event_flags(const shared_conn &conn, int flags) {
    auto const data = conn->as<quic_conn_t>();
    auto &status = data->flags;

    data->speed_min_delay = static_cast<int>(this->config_->speed_check_delay);

    auto const prev = std::exchange(status, ((status >> 2) << 2) | (flags & CONN_MASK_UPDATE));

    if ((status & (ev::READ|ev::DISCONNECT)) == ev::READ && data->ev_callback) {
        if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
            this->global_.flush_custom_read_cb(conn, &this->global_, this);

        this->flush_read_ (conn, data);

        if (status & CONN_RECV_END) {
            if (manapi::net::worker::openssl_quic::call_user_callback(data->ev_callback, conn, CONN_RECV_END, nullptr, 0, nullptr))
                this->close_connection(conn, CLOSE_CONN_ERR);
        }
    }

    return prev;
}

std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::openssl_quic::event_on(
    const shared_conn &conn, std::unique_ptr<worker_watcher_cb> callback) {
    return prepared::event_on(conn, std::move(callback));
}

void manapi::net::worker::openssl_quic::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) {
    prepared::feed_event(this, conn, flags, buff, size, p);
}

bool manapi::net::worker::openssl_quic::is_writable(const shared_conn &conn) {
    auto const data = conn->as<quic_conn_t>();
    return prepared::is_writable(this->config_, conn, data);
}

std::size_t manapi::net::worker::openssl_quic::recv_count(const shared_conn &conn) const {
    return prepared::recv_count(conn);
}

manapi::bytebuffer manapi::net::worker::openssl_quic::recv_first_buffer(const shared_conn &conn) {
    return prepared::recv_first_buffer(conn);
}

ssize_t manapi::net::worker::openssl_quic::sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) {
    return prepared::sync_write(this, conn, buff, nbuff, finish);
}

ssize_t manapi::net::worker::openssl_quic::sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) {
    auto const s = conn->as<quic_conn_t>();
    ssize_t res = 0;

    while (nbuff) {
        int flags = 0;

        if (finish && nbuff == 1)
            flags |= SSL_WRITE_FLAG_CONCLUDE;

        std::size_t written;

        auto rhs = SSL_write_ex2(s->conn, buff->base, buff->len, flags, &written);

        if (rhs!=1) {
            written = interface_worker::connection_io_send(&s->top->send, buff->base, static_cast<ssize_t>(buff->len),
                &this->bufferpool(), this->config_->buffer_size, &s->top->send_size, this->config_->max_buffer_stack);

            res += written;

            if (written == buff->len) {
                if (flags)
                    s->flags |= CONN_SEND_END;

                nbuff--;
                buff++;

                continue;
            }

            break;
        }

        s->transfered += written;
        res += static_cast<ssize_t>(written);

        if (written != buff->len)
            break;

        nbuff--;
        buff++;
    }

    return res;
}

void manapi::net::worker::openssl_quic::waiting(const shared_conn &conn, bool state) {
    return prepared::waiting(conn, state);
}

std::string_view manapi::net::worker::openssl_quic::alpn_ossltest() {
    return this->alpn_ossltest_;
}

void manapi::net::worker::openssl_quic::alpn_ossltest(std::string test) {
    this->alpn_ossltest_ = std::move(test);
}

void manapi::net::worker::openssl_quic::flush_read_(const shared_conn &conn, quic_conn_t *data) {
    return prepared::flush_read_(this, conn, data);
}

void manapi::net::worker::openssl_quic::flush_write_(const shared_conn &conn, quic_conn_t *data) MANAPIHTTP_NOEXPECT {
    if (data->flags & CONN_CLOSED)
        return;

    while (data->top->send.last_deque) {
        int flags = 0;
        bool const last = data->top->send.deque.get() == data->top->send.last_deque;

        if (data->flags & CONN_SEND_END && last)
            flags |= SSL_WRITE_FLAG_CONCLUDE;

        auto &buffer = data->top->send.deque->buffer;

        if (data->top->send.deque_current)
            buffer.shift_add(std::exchange(data->top->send.deque_current, 0));

        std::size_t size;

        if (last)
            size = data->top->send.deque_cursor;
        else
            size = buffer.size();

        std::size_t written;

        auto rhs = SSL_write_ex2(data->conn, buffer.data(), size, flags, &written);

        if (rhs != 1)
            break;

        data->transfered += written;

        if (written == size) {
            data->top->send.deque = std::move(data->top->send.deque->next);
            data->top->send.deque_current = 0;

            if (!data->top->send.deque) {
                data->top->send.deque_cursor = 0;
                data->top->send.last_deque = nullptr;
            }

            data->top->send_size--;

            continue;
        }

        buffer.shift_add(written);
        break;
    }

    if (!data->top->send.last_deque)
        this->feed_event(conn, ev::WRITE, nullptr, 0, nullptr);
}

void manapi::net::worker::openssl_quic::update_limit_rate() {
    /* in event loop */
    for (auto nit = this->conns_.begin(); nit != this->conns_.end(); ) {
        auto conn = nit->second;
        auto next = std::next(nit);
        this->update_limit_rate_connection(conn);
        nit = next;
    }
}

void manapi::net::worker::openssl_quic::update_limit_rate_connection(const shared_conn &sconn) {
    prepared::update_limit_rate_connection (sconn, this, this->config_, &this->global_);
}

void manapi::net::worker::openssl_quic::connection_interface_eraser(worker::connection *n) MANAPIHTTP_NOEXPECT {
    if (!n)
        return;

    auto uptr = std::unique_ptr<worker::connection> (n);
    auto connection = std::unique_ptr<quic_conn_t> (uptr->as<quic_conn_t>());

    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s:Free QUIC %p conn", "openssl_quic", connection.get());

    SSL_free(connection->conn);

    auto const wrk = dynamic_cast<openssl_quic*> (connection->worker);
    if (wrk) {
        if (auto rhs = wrk->global_.cleanup_cb(n, &wrk->global_, wrk))
            manapi_log_error("%s: %s failed due to %d", "openssl_quic", "this->global_.cleanup_cb failed", rhs);

        wrk->count--;
        wrk->worker_data()->as<http::server_ctx::worker_data_t>()->count.fetch_sub(1);

        if (wrk->count < wrk->config_->max_connections) {
            // TODO: start accepting
        }

        openssl_quic::io_event_cb(wrk->w_->custom(), 0, 0);

        if (wrk->flags & NET_WORKER_CLOSED
            && !wrk->count && wrk->finish)
            wrk->finish();
    }
}

void manapi::net::worker::openssl_quic::timeout_event_cb(uv_timer_t *s) MANAPIHTTP_NOEXPECT {
    auto w = static_cast<openssl_quic *>(s->data);
    io_event_cb (w->w_->custom(), 0, 0);
}

void manapi::net::worker::openssl_quic::io_event_cb(uv_poll_t *s, int status, int events) MANAPIHTTP_NOEXPECT {
    ERR_clear_error();

    int rhs;
    timeval tv{};
    int flags = 0;
    int isinfinite;
    auto w = static_cast<openssl_quic *>(s->data);

    assert( w );

    while (true) {
        rhs = SSL_handle_events(MANAPI_AS_SSL(w->listener));
        if (!rhs) {
            ssl_dump_error_(SSL_get_error(MANAPI_AS_SSL(w->listener), rhs), "SSL_handle_events");
        }

        auto client = SSL_accept_connection(MANAPI_AS_SSL(w->listener), 0);

        if (client)
            w->conn_processing(client);

        if (SSL_get_accept_connection_queue_len(MANAPI_AS_SSL(w->listener)))
            continue;

        break;
    }

    for (auto it = w->conns_.begin(); it != w->conns_.end(); ) {
        auto next = std::next(it);
        w->conn_processing(it->second->as<quic_conn_t>()->conn);
        it = next;
    }

    if (SSL_net_read_desired(MANAPI_AS_SSL(w->listener)))
        flags |= ev::READ;

    if (SSL_net_write_desired(MANAPI_AS_SSL(w->listener)))
        flags |= ev::WRITE;

    rhs = w->t_->stop();

    if (!SSL_get_event_timeout(MANAPI_AS_SSL(w->listener), &tv, &isinfinite) && !isinfinite) {
        std::size_t const mil = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        rhs = w->t_->start(mil, 0, timeout_event_cb);

        if (rhs)
            manapi_log_trace("%s: %s failed due to %s", "openssl_quic", "timeout", ev::strerror(rhs));

    }
    else if (!flags) {
        manapi_log_trace(debug::LOG_TRACE_LOW, "openssl_quic doesn't set flags and timeouts");
    }

    if (flags != s->flags) {
        rhs = uv_poll_stop(s);
        if (rhs)
            manapi_log_trace("%s: %s failed due to %s", "openssl_quic", "poll_stop", ev::strerror(rhs));

        rhs = uv_poll_start(s, flags, openssl_quic::io_event_cb);
        if (rhs)
            manapi_log_trace("%s: %s failed due to %s", "openssl_quic", "poll_start", ev::strerror(rhs));
    }
}

void manapi::net::worker::openssl_quic::io_unbind_cb(ev::handle *s) MANAPIHTTP_NOEXPECT {
    try {
        auto w = static_cast<openssl_quic *>(s->data);
        if (w) {
            BIO_closesocket(std::exchange(w->sock, 0));
            w->finish();
        }
        else
            manapi_log_trace(debug::LOG_TRACE_HIGH, "%s: %s failed due to %s", "openssl_quic", "io_unbind_cb", "data is null");
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "openssl_quic", "io_unbind_cb", e.what());
    }
}

void manapi::net::worker::openssl_quic::conn_processing(SSL *client) MANAPIHTTP_NOEXPECT {
    auto app_data = SSL_get_app_data(client);
    shared_conn conn;
    quic_conn_t *sn;

    if (!app_data) {
        try {
            auto p = std::make_unique<quic_conn_t>();
            auto top = std::make_unique<connection_io>();

            auto ipstorage = std::make_unique<worker::connection::ipdata_t>();

            p->conn = client;
            p->worker = this;
            p->top = std::move(top);
            p->flags = 0;

            auto shared = std::shared_ptr<worker::connection> (
                new worker::connection{p.get()}, connection_interface_eraser);

            p.release();

            shared->ipdata = std::move(ipstorage);
            shared->ipdata->len = 0;

            memcpy(shared->ipdata->client.data, &this->config_->server_addr, this->config_->server_len);
            shared->ipdata->len = this->config_->server_len;

            auto res = this->conns_.insert(
                {reinterpret_cast<uintptr_t>(client), std::move(shared)});

            if (!res.second) {
                SSL_free(client);
                return;
            }

            manapi_log_trace("%s: new connection %p", "openssl_quic", client);

            this->count++;
            SSL_set_app_data (client, &res.first->second);

            conn = res.first->second;
        }
        catch (std::exception const &e) {
            manapi_log_error("%s: %s failed due to %s", "openssl_quic", "new connection", e.what());
            SSL_free(client);
            return;
        }

        try {
            if (this->global_.init_cb(conn, &this->global_, this))
                goto err;

            this->event_on(conn,
                std::make_unique<worker_watcher_cb>([this]
                (const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) mutable
                -> void {
                    if (this->global_.accept_cb (conn, flags, buffer, nsize, p, &this->global_, this))
                        this->close_connection(conn, CLOSE_CONN_ERR);
            }));

            this->event_flags(conn, ev::READ);
        }
        catch (std::exception const &e) {
            manapi_log_error("%s: %s failed due to %s", "openssl_quic", "setup connection", e.what());
            goto err;
        }

        sn = (conn)->as<quic_conn_t>();
    }
    else {
        conn = *static_cast<shared_conn *> (app_data);
        sn = (conn)->as<quic_conn_t>();
    }

    try {
        while (true) {
            if (sn->flags & CONN_TLS_SHUTDOWN) {
                auto rhs = SSL_shutdown(sn->conn);

                manapi_log_trace(debug::LOG_TRACE_LOW, "%s: %s %p returned %d", "openssl_quic", "SSL_shutdown()", sn->conn, rhs);

                if (rhs == 1) {
                    this->close_connection(conn, CLOSE_CONN_EOF);
                    break;
                }

                break;
            }

            if (SSL_is_init_finished(sn->conn)) {
                this->flush_write_(conn, sn);

                char buffer[16384];
                std::size_t readbytes;

                while (sn->flags & ev::READ) {
                    if (1 == SSL_read_ex(sn->conn, buffer, sizeof (buffer), &readbytes)
                        && readbytes) {
                        sn->transfered += readbytes;

                        if ((conn)->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
                            this->global_.flush_custom_read_cb(conn, &this->global_, this);
                        else
                            this->feed_event(conn, ev::READ, buffer,
                                static_cast<ssize_t>(readbytes), nullptr);

                        continue;
                    }

                    break;
                }

                if (!SSL_has_pending(sn->conn)) {
                    sn->flags |= CONN_RECV_END;
                    this->feed_event(conn, CONN_RECV_END, buffer,
                                static_cast<ssize_t>(readbytes), nullptr);
                }

                if (sn->flags & ev::WRITE)
                    this->feed_event(conn, ev::WRITE, nullptr, 0, nullptr);
            }
            else {
                auto rhs = SSL_do_handshake(sn->conn);

                manapi_log_trace(debug::LOG_TRACE_LOW, "%s: %s returned %d", "openssl_quic", "SSL_do_handshake()", rhs);

                if (rhs > 0) {
                    /* finished */
                    continue;
                }

                ssl_dump_error_(SSL_get_error(sn->conn, rhs), "SSL_do_handshake()");

                /* do suffer */
            }

            break;
        }
        return;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "openssl_quic", "io_event_cb", e.what());
    }
    err: {
        this->close_connection(conn, CLOSE_CONN_ERR);
    }
}



#endif
