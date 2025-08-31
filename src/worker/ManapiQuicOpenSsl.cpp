#include <memory>
#include <array>

#include "../include/worker/ManapiQuicOpenSsl.hpp"
#include "ManapiProcess.hpp"
#include "ManapiString.hpp"
#include "ManapiDns.hpp"
#include "worker/ManapiBaseUtils.hpp"
#include "std/ManapiAsyncSocket.hpp"
#include "std/ManapiEasyCancellation.hpp"

#ifdef MANAPIHTTP_OPENSSL_QUIC_SUPPORT

#   include <openssl/ssl.h>
#   include <openssl/quic.h>
#   include <openssl/err.h>


#   define MANAPI_AS_BIO(n) static_cast<BIO*>(n)
#   define MANAPI_AS_SSL(n) static_cast<SSL*>(n)
#   define MANAPI_AS_CTX(n_) static_cast<SSL_CTX*>(n_)
#   define MANAPI_AS_BIO_ADDR(n_) (BIO_ADDR*)(n_)

#   define DATA_SIZE_PARTBYTE 4096
#   define DATA_SIZE_TOPBYTE (131072)

static int ssl_session_ctx_id = 1;

enum conn_tls_flags  {
    CONN_QUIC_SHUTDOWN = manapi::net::worker::base::CONN_MAX_CODE << 1,
    CONN_QUIC_EARLY_DATA = CONN_QUIC_SHUTDOWN << 2,
    CONN_QUIC_EARLY_FINISHED = CONN_QUIC_SHUTDOWN << 3,
    CONN_QUIC_SHUTDOWN_FINISHED = CONN_QUIC_SHUTDOWN << 4
};

struct ssl_worker_ctx_t {
    std::map<std::string, SSL_SESSION*, std::less<>> sessions;
    SSL_CTX *ctx;
    manapi::timer sessions_flush_timer;
};

struct ssl_bio_deleter_t {
    void operator() (BIO *b) const MANAPIHTTP_NOEXCEPT {
        BIO_free(b);
    }
};

struct ssl_ctx_deleter_t {
    void operator() (SSL_CTX *b) const MANAPIHTTP_NOEXCEPT {
        SSL_CTX_free(b);
    }
};

struct ssl_bio_addr_deleter {
    void operator() (BIO_ADDR *b) const MANAPIHTTP_NOEXCEPT {
        BIO_ADDR_free(b);
    }
};

struct addrinfofree_deleter {
    void operator () (addrinfo *n) const MANAPIHTTP_NOEXCEPT {
        manapi::ev::getaddrinfo::free(n);
    }
};

struct manapi::net::worker::openssl_quic::quic_conn_t : connection_base2_t {
    SSL *conn;
    manapi::timer timeout;
    openssl_quic *worker;
    std::map<int64_t, shared_conn> streams;
    std::size_t poll_id;
};

struct manapi::net::worker::openssl_quic::quic_stream_t : connection_prepared_t {
    SSL *stream;
    worker::connection *parent;
    std::size_t poll_id;
    uint32_t sent_an_tick;
    uint32_t send_an_tick_state;
};

manapi::net::worker::openssl_quic::openssl_quic(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config)
    : udp(std::move(site), std::move(wdata), config) {
    this->listener = nullptr;
    this->ctx = nullptr;
    this->count = 0;
    this->finish_ref=0;
    this->flags_ = WORKER_BASE_FLAG_MULTISTREAM;
    this->pool_data_ = nullptr;
    this->deep_worker_id_ = 0;
    this->wbio = nullptr;
    this->rbio = nullptr;
}

manapi::net::worker::openssl_quic::~openssl_quic() {
    SSL_free(MANAPI_AS_SSL(this->listener));

    if (this->pool_data_) {
        std::lock_guard<std::mutex> lk (*this->pool_data_->mx);
        auto &wdata = this->pool_data_->data[this->deep_worker_id_];
        if (wdata.ref) {
            if (!(--wdata.ref)) {
                auto ctx_data = static_cast<ssl_worker_ctx_t *> (wdata.data);
                ctx_data->sessions_flush_timer.stop();
                SSL_CTX_free(ctx_data->ctx);
                delete ctx_data;
                wdata.data = nullptr;
            }
        }
    }
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

    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s: %s err=%d, %.*s", "openssl_quic", msg, err, len, buf);
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

int manapi::net::worker::openssl_quic::select_alpn(SSL *ssl, const unsigned char **out, unsigned char *out_len, const unsigned char *in, unsigned int in_len, void *arg) {
    auto const w = static_cast<manapi::net::worker::openssl_quic *>(arg);
    auto const alpn_ossltest = w->alpn_ossltest();
    if (SSL_select_next_proto((unsigned char **)out, out_len, reinterpret_cast<const uint8_t*>(alpn_ossltest.data()),
        static_cast<uint32_t>(alpn_ossltest.size()), in, in_len) == OPENSSL_NPN_NEGOTIATED) {
        manapi_log_trace(debug::LOG_TRACE_LOW, "%s: %p selected %.*s", "openssl_quic", ssl, *out_len, *out);
        if (w->global_.alpn_cb) {
            auto const version = w->global_.alpn_cb( &w->global_, reinterpret_cast<char const *>(*out), *out_len, w);
            if (version >= 0) {
                auto conn = static_cast<shared_conn *>(SSL_get_app_data(ssl));
                if (conn) {
                    (*conn)->version = version;
                }
                else {
                    auto ref = (void*)(&version);
                    if (!SSL_set_app_data(ssl, *(void**)(ref)))
                        return SSL_TLSEXT_ERR_ALERT_FATAL;
                }
            }
        }

        return SSL_TLSEXT_ERR_OK;
    }
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

manapi::error::status manapi::net::worker::openssl_quic::load_params (manapi::net::worker::openssl_quic *w, SSL_CTX *ctx, manapi::json sslconfig) {
    using ci = manapi::internal::config_interface;

    auto verify_peer = ci::get_config_param<bool>(sslconfig, "verify_peer", true);
    auto alpns = ci::get_config_param<std::string>(sslconfig, "alpns", {});
    auto const cipher_list = ci::get_config_param<std::string>(w->config_->ssl, "ciphers", {});
    auto const ssl_v2 = ci::get_config_param<bool>(w->config_->ssl, "ssl_v2", false);
    auto const ssl_v3 = ci::get_config_param<bool>(w->config_->ssl, "ssl_v3", false);
    auto const ticket = ci::get_config_param<bool>(w->config_->ssl, "ticket", true);
    auto const sess_timeout = ci::get_config_param<uint32_t>(w->config_->ssl, "sess_timeout", 300);
    auto const sess_cache = ci::get_config_param<bool>(w->config_->ssl, "sess_cache", false);
    auto const sess_cache_size = ci::get_config_param<uint32_t>(w->config_->ssl, "sess_cache_size", 1024 * 20);
    auto const max_early_data = ci::get_config_param<std::size_t>(w->config_->ssl, "max_early_data", 0);

    auto ktls = http::config::get_config_param<bool>(
        w->config_->ssl, "ktls", true);
    auto single_dh_use = http::config::get_config_param<bool>(
        w->config_->ssl, "single_dh_use", false);
    auto ktls_tx_zerocopy_senfile = http::config::get_config_param<bool>(
        w->config_->ssl, "ktls_tx_zerocopy_senfile", true);

    {
        /* alpns */
        auto list = manapi::string::split(alpns, ',');
        auto config_list = w->config()->alpns();

        list.insert(list.end(), config_list.begin(), config_list.end());

        w->alpn_ossltest(generate_alpn_ossltest(list));


        SSL_CTX_set_verify(ctx, verify_peer, nullptr);
        SSL_CTX_set_alpn_select_cb(ctx, manapi::net::worker::openssl_quic::select_alpn, w);
#ifdef SSL_OP_NO_COMPRESSION
        SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);
#endif
        if (sess_cache) {
            //SSL_SESS_CACHE_NO_INTERNAL_STORE
            SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);

            if (!SSL_CTX_set_session_id_context(ctx,
                reinterpret_cast<const unsigned char *>(&ssl_session_ctx_id), sizeof(ssl_session_ctx_id)))
                goto err;
        }
        else {
            SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
        }
#ifdef SSL_OP_ENABLE_KTLS
        if (ktls)
            SSL_CTX_set_options(ctx, SSL_OP_ENABLE_KTLS);
#endif
#ifdef SSL_OP_ENABLE_KTLS_TX_ZEROCOPY_SENDFILE
        if (ktls_tx_zerocopy_senfile)
            SSL_CTX_set_options(ctx, SSL_OP_ENABLE_KTLS_TX_ZEROCOPY_SENDFILE);
#endif
#ifdef SSL_OP_NO_SSLv2
        if (!ssl_v2)
            SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);
#endif
#ifdef SSL_OP_NO_SSLv3
        if (!ssl_v3)
            SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3);
#endif
#ifdef SSL_OP_NO_TICKET
        if (!ticket)
            SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET);
#endif


        SSL_CTX_sess_set_cache_size(ctx, sess_cache_size);

        if (!SSL_CTX_set_timeout(ctx, sess_timeout))
            goto err;
#ifdef SSL_OP_SINGLE_DH_USE
        if (single_dh_use)
            SSL_CTX_set_options(ctx, SSL_OP_SINGLE_DH_USE);
#endif
        if (cipher_list.empty()) {
            SSL_CTX_set_options(ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
        }
        else {
            // TLSv1.2>= SSL_CTX_set_cipher_list
            // TLSv1.3<= SSL_CTX_set_ciphersuites
            if (!SSL_CTX_set_ciphersuites(ctx, static_cast<const char *>(cipher_list.data())))
                goto err;

        }

        SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS
            |SSL_MODE_AUTO_RETRY
            |SSL_MODE_ENABLE_PARTIAL_WRITE
            |SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER
        );

        return manapi::error::status_ok();
    }
err:
    std::unique_ptr<BIO, ssl_bio_deleter_t> bio;
    bio.reset(BIO_new(BIO_s_mem()));
    ERR_print_errors(bio.get());
    char *buf;
    size_t len = BIO_get_mem_data(bio.get(), &buf);

    manapi_log_error("%s due to %.*s", "openssl_tls:create context failed", len, buf);
    return error::status_internal("openssl_tls:create context failed");
}

manapi::future<manapi::error::status> manapi::net::worker::openssl_quic::init(std::size_t deep) {
    auto udp_res = co_await udp::init(deep + 1);
    if (!udp_res)
        co_return std::move(udp_res);

    try {
        using ci = internal::config_interface;

        this->polls_.reserve(16);
        this->deep_worker_id_ = deep;

        this->pool_data_ = &this->worker_data_->as<http::server_ctx::worker_data_t>()->pools[this->worker_pool_id_];
        std::lock_guard<std::mutex> lk (*this->pool_data_->mx);

        if (this->pool_data_->data.size() <= deep)
            this->pool_data_->data.resize(deep + 1);

        if (!(this->pool_data_->data[deep].ref))
            this->pool_data_->data[deep].data = new ssl_worker_ctx_t{};

        auto ctx_data = static_cast<ssl_worker_ctx_t *>(this->pool_data_->data[deep].data);
        this->pool_data_->data[deep].ref++;

        if (!ctx_data->sessions_flush_timer) {

        }
        if (!ctx_data->ctx) {
            std::unique_ptr<SSL_CTX, ssl_ctx_deleter_t> ctx (SSL_CTX_new(OSSL_QUIC_server_method()));
            if (!ctx)
                co_return error::status_internal("openssl_quic:SSL_CTX_new");

            auto &sslconfig = this->config_->ssl;

            auto const sslcert = ci::get_config_param<std::string>(sslconfig, "cert", {});
            auto const sslkey = ci::get_config_param<std::string>(sslconfig, "key", {});

            auto res = load_certs(ctx.get(), sslcert, sslkey);
            if (!res) {
                co_return std::move(res);
            }

            res = load_params(this, ctx.get(), std::move(sslconfig));
            if (!res) {
                co_return std::move(res);
            }

            ctx_data->ctx = ctx.release();
        }

        this->ctx = ctx_data->ctx;

        std::unique_ptr<BIO, ssl_bio_deleter_t> wbio;
        std::unique_ptr<BIO, ssl_bio_deleter_t> rbio;
        //
        // BIO *wbiop;
        // BIO *rbiop;
        //
        // if (!BIO_new_bio_dgram_pair(&rbiop, 0, &wbiop, 0))
        //     co_return error::status_internal("openssl_quic:BIO_new");
        //
        // wbio.reset(wbiop);
        // rbio.reset(rbiop);
        //
        wbio.reset(BIO_new(BIO_s_dgram_mem()));
        rbio.reset(BIO_new(BIO_s_dgram_mem()));

        if (!wbio || !rbio) {
            co_return error::status_internal("openssl_quic:BIO_new");
        }
        auto const listener = SSL_new_listener (MANAPI_AS_CTX(this->ctx), 0);
        if (!listener)
            co_return error::status_internal("openssl_quic:SSL_new_listener");

        if (!SSL_set_blocking_mode(listener, 0))
            co_return error::status_internal("openssl_quic:SSL_set_blocking_mode");

        SSL_set_accept_state(listener);
        this->rbio = rbio.get();
        this->wbio = wbio.get();

        if (!BIO_dgram_set_caps(this->rbio, BIO_DGRAM_CAP_HANDLES_DST_ADDR|BIO_DGRAM_CAP_HANDLES_SRC_ADDR|BIO_DGRAM_CAP_PROVIDES_DST_ADDR|BIO_DGRAM_CAP_PROVIDES_SRC_ADDR)
            || !BIO_dgram_set_caps(this->wbio, BIO_DGRAM_CAP_HANDLES_DST_ADDR|BIO_DGRAM_CAP_HANDLES_SRC_ADDR|BIO_DGRAM_CAP_PROVIDES_DST_ADDR|BIO_DGRAM_CAP_PROVIDES_SRC_ADDR)) {
            co_return error::status_internal("openssl_quic:BIO_dgram_set_caps");
        }

        if (!BIO_dgram_set_local_addr_enable(this->rbio, 1) ||
            !BIO_dgram_set_local_addr_enable(this->wbio, 1))
            co_return error::status_internal("openssl_quic:BIO_dgram_set_local_addr_enable");

        if (!BIO_dgram_set0_local_addr(this->wbio, &this->config_->server_addr) ||
            !BIO_dgram_set0_local_addr(this->rbio, &this->config_->server_addr))
            co_return error::status_internal("openssl_quic:BIO_dgram_set0_local_addr");

        SSL_set_bio(listener, rbio.release(), wbio.release());

        if (!SSL_listen(listener))
            co_return error::status_internal("openssl_quic:SSL_listen");

        this->listener = listener;

        auto loop = manapi::async::current()->eventloop()->loop();
        if (!loop)
            co_return error::status_not_found("ev:loop");
        this->t_ = std::make_unique<ev::timer>();

        auto rhs = this->t_->bind(loop);

        if (rhs) {
            manapi_log_trace("%s failed due to %s", "openssl_quic:uv timer bind", ev::strerror(rhs));
            co_return error::status_internal("openssl_quic:uv timer bind");
        }

        this->t_->data(this);

        SSL_POLL_ITEM poll_item;

        poll_item.desc = SSL_as_poll_descriptor(listener);
        poll_item.events = SSL_POLL_EVENT_IC|SSL_POLL_EVENT_EL|SSL_POLL_EVENT_F;
        poll_item.revents = 0;

        this->polls_.push_back(poll_item);

        auto timer = manapi::async::current()->timerpool()->append_interval_sync(
            1000, [this] (const manapi::timer &t) -> void {
                this->update_limit_rate();
            });

        if (!timer.ok())
            co_return timer.err();

        this->update_limit_timer = timer.unwrap();

        rhs = this->udp_accept_->recv_start();
        if (rhs) {
            manapi_log_trace("%s failed due to %s", "openssl_quic:recv_start", ev::strerror(rhs));
            co_return error::status_internal("openssl_quic:recv_start");
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
        if (this->count) {
            this->flags_ |= WORKER_BASE_FLAG_CLOSED;
            this->finish = std::move(cb);
        }
        else {
            if (this->t_) {
                if (auto err = this->t_->stop()) {
                    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH,"%s due to %s",
                        "openssl_quic:timer stop failed", ev::strerror(err));
                }

                this->finish_ref++;
                this->t_->unbind(io_unbind_cb);
            }

            if (this->update_limit_timer) {
                this->update_limit_timer.stop();
                this->update_limit_timer = nullptr;
            }

            this->listener = nullptr;
            this->ctx = nullptr;

            if (this->finish_ref) {
                this->finish = std::move(cb);
            }
            else {
                udp::stop(std::move(cb));
            }
        }

        return;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "openssl_quic", "stop", e.what());
    }

    if (cb)
        cb();
}

void manapi::net::worker::openssl_quic::close_connection(shared_conn conn, int flags) MANAPIHTTP_NOEXCEPT {
    if (!conn)
        return;

    if (conn->wrk.flags & WRK_INTERFACE_IS_STREAM) {
        this->close_stream(conn, flags);
        return;
    }

    auto s = conn->as<quic_conn_t>();

    if (s->flags & CONN_REMOVED)
        return;

    if (flags == CLOSE_CONN_EOF)
        s->flags |= CONN_QUIC_SHUTDOWN_FINISHED;
    else {
        if ((s->flags & (CONN_QUIC_SHUTDOWN|CONN_QUIC_SHUTDOWN_FINISHED)) == CONN_QUIC_SHUTDOWN) {
            this->conn_processing(s->conn);
            return;
        }
    }

    s->flags |= CONN_WANT_CLOSE;
    bool custom_shutdown = (this->global_.flags_cb(conn, &this->global_, this) & WRK_GLOBAL_FLAG_SHUTDOWN_SUPPORTED);


    s->flags |= CONN_REMOVED;

    if (flags & (CLOSE_CONN_ERR|CLOSE_CONN_EOF)) {
        for (auto it = s->streams.begin(); it != s->streams.end(); ) {
            auto next = std::next(it);
            this->rst_stream(it->second);
            it = next;
        }
    }

    if (custom_shutdown) {
        manapi_log_trace(debug::LOG_TRACE_LOW, "openssl_quic: custom shutdown on %p", s->conn);
        auto rhs = this->global_.shutdown_cb(conn, &this->global_, this, false);
        if (!rhs) {
            s->flags ^= CONN_REMOVED;
            return;
        }
    }

    manapi_log_trace(debug::LOG_TRACE_LOW, "openssl_quic: rst all streams on %p (%zu)", s->conn, s->streams.size());

    for (auto it = s->streams.begin(); it != s->streams.end(); ) {
        auto next = std::next(it);
        this->rst_stream(it->second);
        it = next;
    }


    s->flags ^= CONN_REMOVED;

    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "openssl_quic:close_connection() %p flags=%d", s->conn, flags);

    prepared::timer_clear(std::move(s->timeout));

    if (flags != CLOSE_CONN_EOF && !(s->flags & (CONN_QUIC_SHUTDOWN|CONN_QUIC_SHUTDOWN_FINISHED))) {
        auto rhs = manapi::async::current()->timerpool()->append_timer_sync(
            2000, [conn] (const manapi::timer &t) mutable -> void {
            auto const s = conn->as<quic_conn_t>();
            s->worker->close_connection(std::move(conn), CLOSE_CONN_EOF);
        });

        if (rhs.ok()) {
            s->timeout = rhs.unwrap();

            s->flags |= CONN_QUIC_SHUTDOWN;

            this->conn_processing(s->conn);
            this->bio_flush_write();

            return;
        }

        s->flags |= CLOSE_CONN_ERR;
    }

    if (!s->streams.empty())
        return;

    s->flags |= CONN_REMOVED|CONN_CLOSED;

    MANAPIHTTP_MUST_ALLOC_START
    manapi::async::current()->etaskpool()->append_task([conn] () -> void {
        std::array<char, 17> arr;
        memset(arr.data(), '\0', arr.size());
        auto const addr = reinterpret_cast<sockaddr *> (conn->ipdata->client.data);
        *arr.data() = static_cast<char>(http::version_ip_by_addr (addr));
        http::ip_by_addr(addr, arr.data() + 1);
        auto s = conn->as<quic_conn_t>();
        auto const w = s->worker;

        if (s->poll_id)
            w->remove_poll_id(std::exchange(s->poll_id, 0));


        // prepared::event_callback_clear(conn, s);
        // prepared::top_buffer_clear(s);


        auto conn_by_ip = w->conns_.find(std::string_view(arr.data(), arr.size()));
        assert(conn_by_ip != w->conns_.end());
        if (conn_by_ip != w->conns_.end()) {
            auto cit = conn_by_ip->second.find(reinterpret_cast<std::uintptr_t>(s->conn));
            assert(cit != conn_by_ip->second.end());
            if (cit != conn_by_ip->second.end())
                conn_by_ip->second.erase(cit);
        }

        if (!SSL_set_app_data(s->conn, nullptr)) {
            /* who cares */
        }

        w->bio_flush_write();
    });
    MANAPIHTTP_MUST_ALLOC_END
}

int manapi::net::worker::openssl_quic::event_flags(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    return prepared::event_flags(conn);
}

int manapi::net::worker::openssl_quic::event_flags(const shared_conn &conn, int flags) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<quic_stream_t>();

    MANAPIHTTP_WORKER_EVENT_LOOP_STREAM(data) {
        if ((status & (ev::READ|ev::DISCONNECT)) == ev::READ && data->ev_callback) {
            if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
                this->global_.flush_custom_read_cb(conn, &this->global_, this);

            this->flush_read_ (conn, data);

            if (status & CONN_RECV_END) {
                if (manapi::net::worker::openssl_quic::call_user_callback(&data->ev_callback, conn, CONN_RECV_END, nullptr, 0, nullptr))
                    this->close_connection(conn, CLOSE_CONN_ERR);
            }
        }

        MANAPIHTTP_WORKER_EVENT_BREAK(data)
    }

    return prev;
}

manapi::net::worker::worker_watcher_cb manapi::net::worker::openssl_quic::event_on(
    const shared_conn &conn, worker_watcher_cb callback) MANAPIHTTP_NOEXCEPT {
    assert(conn->wrk.flags & WRK_INTERFACE_IS_STREAM);
    return prepared::event_on(conn, std::move(callback));
}

void manapi::net::worker::openssl_quic::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT {
    prepared::feed_event(this, conn, flags, buff, size, p);
}

bool manapi::net::worker::openssl_quic::is_writable(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<quic_stream_t>();
    return prepared::is_writable(this->config_, conn, data);
}

std::size_t manapi::net::worker::openssl_quic::recv_count(const shared_conn &conn) const MANAPIHTTP_NOEXCEPT {
    return prepared::recv_count(conn);
}

manapi::bytebuffer manapi::net::worker::openssl_quic::recv_first_buffer(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    return prepared::recv_first_buffer(conn);
}

ssize_t manapi::net::worker::openssl_quic::sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT {
    return prepared::sync_write(this, conn, buff, nbuff, finish);
}

ssize_t manapi::net::worker::openssl_quic::sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, std::size_t maxcnt) MANAPIHTTP_NOEXCEPT {
    auto const s = conn->as<quic_stream_t>();
    ssize_t res = 0;

    if (s->flags & ev::DISCONNECT)
        return -1;

    assert(!(s->flags & CONN_SEND_END));

    if (prepared::write_buffs_is_full(s->top.get(), maxcnt))
        return 0;

    while (nbuff) {
        int flags = 0;

        if (finish && nbuff == 1)
            flags |= SSL_WRITE_FLAG_CONCLUDE;

        std::size_t written = 0;
        int rhs;

        if (s->sent_an_tick > s->send_an_tick_state) {
            rhs = 1;
            written = 0;
        }
        else {
            if (!s->top->send_size) {
                ERR_clear_error();
                rhs = SSL_write_ex2(s->stream, buff->base, buff->len, flags, &written);
                s->sent_an_tick += static_cast<decltype(s->sent_an_tick)>(written);
                 if (s->sent_an_tick > s->send_an_tick_state) {
                     try {
                         manapi::async::current()->etaskpool()->append_task(
                             [this, conn, s] () -> void {
                             if (s->flags & ev::DISCONNECT)
                                 return;

                             s->sent_an_tick = 0;
                             if (s->send_an_tick_state < DATA_SIZE_TOPBYTE)
                                 s->send_an_tick_state += DATA_SIZE_PARTBYTE;

                             this->flush_write_(conn, s);
                             this->feed_event(conn, ev::WRITE, nullptr, 0, nullptr);
                         });
                     }
                     catch (...) {
                         return -1;
                     }
                 }
            }
            else {
                rhs = 1;
                written = 0;
            }
        }

        if (rhs!=1) {
            assert(!written);
            auto err = SSL_get_error(s->stream, rhs);
            switch (err) {
                case SSL_ERROR_WANT_READ:
                case SSL_ERROR_WANT_WRITE:
                    this->bio_flush_write ();
                break;
                default: {
                    auto bioerr = ERR_peek_error();
                    if (bioerr && !BIO_err_is_non_fatal(bioerr)) {
                        ssl_dump_error_(err, "SSL_write_ex2");
                        ERR_clear_error();
                        return -1;
                    }
                    ERR_clear_error();
                    break;
                }
            }
        }

        s->transfered += written;

        this->bio_flush_write();

        if (!written) {
            auto sent = interface_worker::connection_io_send(&s->top->send, buff->base, static_cast<ssize_t>(buff->len),
                &this->bufferpool(), this->config_->buffer_size, &s->top->send_size, maxcnt);

            if (sent < 0)
                return -1;

            res += sent;

            if (sent == buff->len) {
                if (flags)
                    s->flags |= CONN_SEND_END;

                nbuff--;
                buff++;

                continue;
            }

            break;
        }

        res += static_cast<ssize_t>(written);

        if (written == buff->len) {
            nbuff--;
            buff++;
            continue;
        }

        buff->base += written;
        buff->len -= static_cast<decltype(buff->len)>(written);
    }

    return res;
}

void manapi::net::worker::openssl_quic::waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXCEPT {
    return prepared::waiting(conn, state);
}

std::string_view manapi::net::worker::openssl_quic::alpn_ossltest() MANAPIHTTP_NOEXCEPT {
    return this->alpn_ossltest_;
}

void manapi::net::worker::openssl_quic::alpn_ossltest(std::string test) MANAPIHTTP_NOEXCEPT {
    this->alpn_ossltest_ = std::move(test);
}

void manapi::net::worker::openssl_quic::close_stream(shared_conn s, int flags) MANAPIHTTP_NOEXCEPT {
    auto const data = s->as<quic_stream_t>();

    if (!data)
        return;

    if (data->flags & CONN_REMOVED)
        return;

    bool custom_shutdown = (this->global_.flags_cb(s, &this->global_, this) & WRK_GLOBAL_FLAG_SHUTDOWN_SUPPORTED);

    data->flags |= CONN_REMOVED;


    if (custom_shutdown) {
        auto res = this->global_.shutdown_cb(s, &this->global_, this, false);
        if (!res) {
            data->flags ^= CONN_REMOVED;
            return;
        }
    }

    data->flags |= CONN_CLOSED|CONN_REMOVED;

    SSL_STREAM_RESET_ARGS reset_args{};
    reset_args.quic_error_code = OSSL_QUIC_ERR_NO_ERROR;
    auto stream_status = SSL_stream_reset(data->stream, &reset_args, sizeof (reset_args));
    //if (stream_status) {
        ssl_dump_error_(SSL_get_error(data->stream, stream_status), "SSL_stream_reset");
    //}

    this->bio_flush_write();

    prepared::event_callback_clear(s, data);

    prepared::top_buffer_clear(data);

    MANAPIHTTP_MUST_ALLOC_START
    manapi::async::current()->etaskpool()->append_task([s, flags] () mutable -> void {
        if (s) {
            auto const data = s->as<quic_stream_t>();
            if (!data)
                return;

            auto const conn_data = data->parent->as<quic_conn_t>();
            auto w = conn_data->worker;

            if (data->poll_id)
                w->remove_poll_id(std::exchange(data->poll_id, 0));

            auto const stream_id = SSL_get_stream_id(data->stream);;

            auto it = conn_data->streams.find(stream_id);

            if (it != conn_data->streams.end())
                conn_data->streams.erase(it);

            if (!SSL_set_app_data(data->stream, nullptr)) {
                /* who cares */
            }

            SSL_free(std::exchange(data->stream, nullptr));

            w->bio_flush_write();

            if (conn_data->streams.empty()) {
                auto const app_data = SSL_get_app_data(conn_data->conn);
                assert(app_data);
                auto conn = *static_cast<shared_conn *>(app_data);
                w->waiting(conn, true);
                if ((conn_data->flags & CONN_WANT_CLOSE)) {
                    w->close_connection(conn, CLOSE_CONN_SHUTDOWN);
                }
            }
        }
    });
    MANAPIHTTP_MUST_ALLOC_END
}

void manapi::net::worker::openssl_quic::rst_stream(shared_conn s) MANAPIHTTP_NOEXCEPT {
    auto data = s->as<quic_stream_t>();
    assert(data);

    data->flags |= CONN_CLOSED;

    this->close_stream(s, CLOSE_CONN_ERR);
}

manapi::net::worker::connection::ipdata_t * manapi::net::worker::openssl_quic::ipdata(worker::connection *conn) MANAPIHTTP_NOEXCEPT {
    auto const s = conn->as<quic_stream_t>();
    return base::ipdata(s->parent);
}

manapi::error::status_or<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::openssl_quic::new_stream(const shared_conn &conn, int flags) MANAPIHTTP_NOEXCEPT {
    try {
        if (!conn)
            goto args;

        auto const data = conn->as<quic_conn_t>();

        if (!data)
            goto args;


        int openssl_flags = 0;

        if (flags & CONN_STREAM_FLAG_UNI)
            openssl_flags |= SSL_STREAM_FLAG_UNI;

        auto stream = SSL_new_stream(data->conn, openssl_flags);
        if (!stream)
            return error::status_resource_exhausted();

        auto res = this->stream_accept(conn, stream);
        if (!res.ok())
            return res.err();

        auto stream_conn = res.unwrap();

        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "openssl_quic: new local stream %p was created using %p id=%zu",
            stream, data->conn, this->stream_id(stream_conn));

        return std::move(stream_conn);
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "openssl_quic:new_stream", e.what());
        return error::status_internal("openssl_quic:new_stream");
    }
    args: return error::status_invalid_argument("new_stream");
}

int64_t manapi::net::worker::openssl_quic::stream_id(const shared_conn &s) MANAPIHTTP_NOEXCEPT {
    return static_cast<int64_t>(SSL_get_stream_id(s->as<quic_stream_t>()->stream));
}

manapi::net::worker::shared_conn manapi::net::worker::openssl_quic::stream_id(const shared_conn &conn, int64_t id) MANAPIHTTP_NOEXCEPT {
    assert(!(conn->wrk.flags & WRK_INTERFACE_IS_STREAM));
    auto const data = conn->as<quic_conn_t>();
    auto it = data->streams.find(id);
    if (it == data->streams.end())
        return nullptr;
    return it->second;
}

std::size_t manapi::net::worker::openssl_quic::streams_size(const shared_conn &conn) const MANAPIHTTP_NOEXCEPT {
    assert(conn && !(conn->wrk.flags & WRK_INTERFACE_IS_STREAM));
    return conn->as<quic_conn_t>()->streams.size();
}

void manapi::net::worker::openssl_quic::bio_flush_write() MANAPIHTTP_NOEXCEPT {
    ERR_clear_error();
    try {
        char buffer[2000];
        sockaddr_storage storage_peer{};
        sockaddr_storage storage_local{};

        while (this->pending_writes < 128) {
            BIO_MSG msg[1]{};
            msg->data = buffer;
            msg->data_len = sizeof (buffer);
            msg->peer = MANAPI_AS_BIO_ADDR(&storage_peer);
            msg->local = MANAPI_AS_BIO_ADDR(&storage_local);

            std::size_t msgs_processed;
            auto constexpr num_msgs = sizeof (msg) / sizeof (BIO_MSG);
            auto rhs = BIO_recvmmsg(this->wbio, msg, sizeof (BIO_MSG), num_msgs, 0, &msgs_processed);
            if (rhs != 1) {
                auto err = ERR_peek_error();
                if (err && !BIO_err_is_non_fatal(err))
                    ssl_dump_error_(SSL_get_error(MANAPI_AS_SSL(this->listener), rhs), "BIO_recvmmsg");
                ERR_clear_error();
                break;
            }

            for (std::size_t i = 0; i < msgs_processed; i++) {
                ev::buff_t buff;
                buff.base = static_cast<char *>(msg->data);
                buff.len = static_cast<decltype(buff.len)>(msg->data_len);

                if (!this->pending_writes)
                    rhs = this->udp_accept_->try_send(&buff, 1, reinterpret_cast<sockaddr *>(&storage_local));
                else
                    rhs = ev::ERR_AGAIN;

                if (rhs != msg->data_len) {
                    if (rhs == ev::ERR_AGAIN) {
                        std::unique_ptr<manapi::ev::buff_t, ev::buffer_deleter> buffs (new manapi::ev::buff_t{});

                        buffs->len = buff.len;
                        buffs->base = new char[buff.len];

                        std::unique_ptr<char, ev::chars_deleter> dest_addr_storage;

                        auto dst_addr = reinterpret_cast<sockaddr *>(&storage_local);
                        auto const socklen = async::socklen (dst_addr);
                        dest_addr_storage.reset(new char[socklen]);

                        memcpy (dest_addr_storage.get(), dst_addr, socklen);

                        auto buffptr = buffs.get();
                        auto send_to = reinterpret_cast<sockaddr *>(dest_addr_storage.get());

                        manapi::ev::udp_send_cb cb = [this, buffs = std::move(buffs), dest_addr_storage = std::move(dest_addr_storage)]
                                (const manapi::ev::shared_udp_send &n, int status)
                                    -> void {

                            this->pending_writes--;

                            if (status) {
                                manapi_log_trace (manapi::debug::LOG_TRACE_MEDIUM, "udp_send:Send failed due to %s(%d):%s",
                                    manapi::ev::namerror(status), status, manapi::ev::strerror(status));
                            }

                            try {
                                if (!this->pending_writes && this->udp_accept_)
                                    this->onrecv(this->udp_accept_, nullptr, 0, nullptr, 0);
                            }
                            catch (std::exception const &e) {
                                manapi_log_error("%s:%s failed due to %s", "openssl_quic", "onrecv", e.what());
                            }
                        };

                        auto res = manapi::async::current()->eventloop()->create_watcher_udp_send(
                            this->udp_accept_.get(), std::move(cb), buffptr, 1, send_to);

                        res.unwrap();

                        this->pending_writes++;
                    }
                    else {
                        manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM,
                            "%s: %s failed due to %s", "openssl_quic", "udp::try_write", ev::strerror(rhs));
                    }
                }
            }

            if (msgs_processed != num_msgs) {
                break;
            }
        }
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s failed due to %s", "bio_flush_write", e.what());
    }
}

void manapi::net::worker::openssl_quic::remove_poll_id(std::size_t poll_id) MANAPIHTTP_NOEXCEPT {
    if (this->polls_.size() <= poll_id)
        return;

    if (this->polls_.size() == poll_id + 1) {
        this->polls_.pop_back();
    }
    else {
        auto &m = this->polls_[poll_id];
        m = this->polls_.back();
        this->polls_.pop_back();

        auto &ssl = m.desc.value.ssl;

        if (SSL_is_listener(ssl))
            return;

        auto data = static_cast<shared_conn*>(SSL_get_app_data(ssl));

        assert(data);

        auto &conn = *data;

        if (conn->wrk.flags & WRK_INTERFACE_IS_STREAM) {
            auto s = conn->as<quic_stream_t>();
            s->poll_id = poll_id;
        }
        else {
            auto s = conn->as<quic_conn_t>();
            s->poll_id = poll_id;
        }
    }
}

void manapi::net::worker::openssl_quic::flush_read_(const shared_conn &conn, quic_stream_t *data) MANAPIHTTP_NOEXCEPT {
    return prepared::flush_read_(this, conn, data);
}

void manapi::net::worker::openssl_quic::flush_write_(const shared_conn &conn, quic_stream_t *data) MANAPIHTTP_NOEXCEPT {
    if (data->flags & CONN_CLOSED)
        return;

    ERR_clear_error();

    while (data->top->send.last_deque) {
        int flags = 0;
        bool const last = data->top->send.deque.get() == data->top->send.last_deque;

        if (data->flags & CONN_SEND_END && last)
            flags |= SSL_WRITE_FLAG_CONCLUDE;

        auto &buffer = data->top->send.deque->buffer;

        // if (data->top->send.deque_current)
        //     buffer.shift_add(std::exchange(data->top->send.deque_current, 0));

        std::size_t size;

        if (last)
            size = data->top->send.deque_cursor;
        else
            size = buffer.size() - data->top->send.deque_current;

        std::size_t written;

        if (size) {
            auto rhs = SSL_write_ex2(data->stream, buffer.data() + data->top->send.deque_current, size, flags, &written);

            if (rhs!=1) {
                assert(!written);
                auto err = SSL_get_error(data->stream, rhs);
                switch (err) {
                    case SSL_ERROR_WANT_READ:
                    case SSL_ERROR_WANT_WRITE:
                        this->bio_flush_write ();
                    break;
                    default: {
                        auto bioerr = ERR_peek_error();
                        if (bioerr && !BIO_err_is_non_fatal(bioerr))
                            ssl_dump_error_(err, "SSL_write_ex2");
                        ERR_clear_error();

                        break;
                    }
                }

                this->bio_flush_write();
                break;
            }

            data->transfered += written;
            this->bio_flush_write();
        }
        else
            written = 0;

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

        data->top->send.deque_current += static_cast<decltype(data->top->send.deque_current)>(written);

        break;
    }

    if (!data->top->send.last_deque)
        this->feed_event(conn, ev::WRITE, nullptr, 0, nullptr);
}

void manapi::net::worker::openssl_quic::update_limit_rate() MANAPIHTTP_NOEXCEPT {
    /* in event loop */
    for (auto nit = this->conns_.begin(); nit != this->conns_.end(); ) {
        auto &conns = nit->second;
        if (conns.empty()) {
            nit = this->conns_.erase(nit);
            continue;
        }

        for (auto cit = conns.begin(); cit != conns.end(); ) {
            auto next = std::next(cit);
            auto conn = cit->second;
            this->update_limit_rate_connection(conn);
            cit = next;
        }

        nit++;
    }
}

void manapi::net::worker::openssl_quic::update_limit_rate_connection(const shared_conn &sconn) MANAPIHTTP_NOEXCEPT {
    auto conn_data = sconn->as<quic_conn_t>();
    auto &config = this->config_;

    for (auto it = conn_data->streams.begin(); it != conn_data->streams.end(); ++it) {
        try {
            auto data = it->second->as<quic_stream_t>();

            conn_data->transfered += data->transfered;

            if (data->transfered >= config->speed_limit_rate
                && data->ev_callback) {
                data->transfered = 0;

                if (!(data->flags & ev::DISCONNECT) && data->flags & ev::WRITE && data->ev_callback) {
                    manapi::async::current()->etaskpool()->append_task([conn = it->second] () -> void {
                        auto data = conn->as<quic_stream_t>();
                        if (!(data->flags & ev::DISCONNECT)) {
                            assert(data->parent);
                            auto conn_data = data->parent->as<quic_conn_t>();
                            assert(conn_data);
                            auto const w = conn_data->worker;
                            assert(w);
                            if (manapi::net::worker::base::call_user_callback(&data->ev_callback, conn, ev::WRITE, nullptr, 0, nullptr)) {
                                w->close_connection(conn, CLOSE_CONN_ERR);
                                return;
                            }
                        }
                    });
                }
            }
            else {
                data->transfered_k += data->transfered;

                if (--data->speed_min_delay <= 0) {
                    if (data->flags & (base::CONN_IO_WAITING) && (data->transfered_k < config->speed_stream_check_bytes)) {
                        manapi::async::current()->etaskpool()->append_task([conn = it->second] () -> void {
                            auto data = conn->as<quic_stream_t>();
                            assert(data && data->parent);
                            auto conn_data = data->parent->as<quic_conn_t>();
                            assert(conn_data && conn_data->worker);
                            conn_data->worker->close_connection(conn, CLOSE_CONN_ERR);
                        });

                        continue;
                    }
                    data->transfered_k = 0;
                    data->speed_min_delay = static_cast<int>(config->speed_stream_check_delay);
                }
                data->transfered = 0;
            }
        }
        catch (std::exception const &e) {
            manapi_log_error("%s: %s failed due to %s", "openssl_quic",
                "update_limit_rate_connection", e.what());
        }
    }

    conn_data->transfered_k += conn_data->transfered;

    if (--conn_data->speed_min_delay <= 0) {
        if (conn_data->flags & (base::CONN_IO_WAITING) && (conn_data->transfered_k < config->speed_check_bytes)) {
            this->close_connection(sconn, CLOSE_CONN_ERR);
            return;
        }
        conn_data->transfered_k = 0;
        conn_data->speed_min_delay = static_cast<int>(config->speed_check_delay);
    }
    conn_data->transfered = 0;
}

void manapi::net::worker::openssl_quic::stream_interface_eraser(worker::connection *n) MANAPIHTTP_NOEXCEPT {
    if (!n)
        return;

    auto uptr = std::unique_ptr<worker::connection> (n);
    auto connection = std::unique_ptr<quic_stream_t> (uptr->as<quic_stream_t>());

    auto conn_data = connection->parent->as<quic_conn_t>();
    auto const conn_ptr = static_cast<shared_conn *>(SSL_get_app_data(conn_data->conn));
    shared_conn conn{nullptr};
    if (conn_ptr)
        conn = *conn_ptr;

    auto const wrk =  (conn_data->worker);


    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s:Free QUIC %p(%zu) stream using %p conn",
        "openssl_quic", connection.get(), SSL_get_stream_id(connection->stream), conn_data->conn);

    if (connection->stream)
        SSL_free(connection->stream);


    /**
     * Decrease count of streams
     */
    if (wrk) {
        wrk->bio_flush_write();

        if (auto rhs = wrk->global_.cleanup_cb(n, &wrk->global_, wrk))
            manapi_log_error("%s: %s failed due to %d", "openssl_quic", "this->global_.cleanup_cb failed", rhs);

        if (conn_data->streams.empty() && conn) {
            if (conn_data->flags & CONN_WANT_CLOSE) {
                wrk->close_connection(conn, CLOSE_CONN_SHUTDOWN);
            }
        }
    }
}

void manapi::net::worker::openssl_quic::connection_interface_eraser(worker::connection *n) MANAPIHTTP_NOEXCEPT {
    if (!n)
        return;

    auto uptr = std::unique_ptr<worker::connection> (n);
    auto connection = std::unique_ptr<quic_conn_t> (uptr->as<quic_conn_t>());

    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s:Free QUIC %p conn", "openssl_quic", connection.get());

    SSL_free(connection->conn);

    auto const wrk =  (connection->worker);
    if (wrk) {
        wrk->bio_flush_write();

        if (n->wrk.data) {
            if (auto rhs = wrk->global_.cleanup_cb(n, &wrk->global_, wrk))
                manapi_log_error("%s: %s failed due to %d", "openssl_quic", "this->global_.cleanup_cb failed", rhs);
        }

        wrk->count--;
        wrk->worker_data()->as<http::server_ctx::worker_data_t>()->count.fetch_sub(1);

        if (wrk->count < wrk->config_->max_connections) {
            // TODO: start accepting
        }

        if (wrk->flags_ & WORKER_BASE_FLAG_CLOSED
            && !wrk->count && wrk->finish) {
            wrk->stop(std::move(wrk->finish));
        }
    }
}

void manapi::net::worker::openssl_quic::timeout_event_cb(uv_timer_t *s) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = static_cast<openssl_quic *>(s->data);
        if (w->udp_accept_)
            w->onrecv(w->udp_accept_, nullptr, 0, nullptr, 0);
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s failed due to %s", "timeout_event_cb", e.what());
    }
}

void manapi::net::worker::openssl_quic::onrecv(const std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned rflags) {
    ERR_clear_error();

    int rhs;
    timeval tv{};
    int isinfinite;

    while (addr) {
        std::size_t msgs_processed;
        BIO_MSG msg{};
        msg.data = buff;
        msg.data_len = size;
        msg.peer = MANAPI_AS_BIO_ADDR(&this->config_->server_addr);
        msg.local = MANAPI_AS_BIO_ADDR(addr);
        rhs = BIO_sendmmsg(this->rbio, &msg, sizeof (BIO_MSG), 1, 0, &msgs_processed);

        if (rhs != 1) {
            ssl_dump_error_(SSL_get_error(MANAPI_AS_SSL(this->listener), rhs), "BIO_sendmmsg");
            return;
        }

        if (!msgs_processed)
            continue;

        break;
    }

    rhs = SSL_handle_events(MANAPI_AS_SSL(this->listener));
    if (rhs != 1) {
        ssl_dump_error_(SSL_get_error(MANAPI_AS_SSL(this->listener), rhs), "SSL_handle_events");
    }

    std::size_t result;
    timeval poll_tv{0};
    std::array<char, 17> arr;
    memset(arr.data(), '\0', arr.size());
    *arr.data() = static_cast<char>(http::version_ip_by_addr (addr));
    http::ip_by_addr(addr, arr.data() + 1);

    rhs = SSL_poll(this->polls_.data(), this->polls_.size(), sizeof (SSL_POLL_ITEM), &poll_tv, SSL_POLL_FLAG_NO_HANDLE_EVENTS, &result);
    if (rhs) {
        if (result) {
            for (std::size_t i = 0; i < this->polls_.size(); ) {
                auto it = &this->polls_[i];

                if (it->revents == SSL_POLL_EVENT_NONE) {
                    i++;
                    continue;
                }

                auto current = it->desc.value.ssl;

                uint64_t processed_event = 0;

                /* new connection */
                if (it->revents & SSL_POLL_EVENT_IC) {
                    while (true) {
                        auto client = SSL_accept_connection(MANAPI_AS_SSL(this->listener), 0);
                        SSL_set_mode(client,SSL_MODE_RELEASE_BUFFERS
                            |SSL_MODE_AUTO_RETRY
                            |SSL_MODE_ENABLE_PARTIAL_WRITE
                            |SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER
                        );
                        if (client) {
                            auto res = this->conn_accept(client, addr);
                            it = &this->polls_[i];
                            if (!res.ok()) {
                                /* who cares */
                            }

                            continue;
                        }

                        break;
                    }
                    processed_event |= SSL_POLL_EVENT_IC;
                }

                /* new incoming stream */
                if (it->revents & SSL_POLL_EVENT_ISB ||
                    it->revents & SSL_POLL_EVENT_ISU) {
                    while (true) {
                        auto conn = *static_cast<shared_conn *>(SSL_get_app_data(it->desc.value.ssl));
                        auto stream = SSL_accept_stream(it->desc.value.ssl, 0);
                        if (stream) {
                            auto res = this->stream_accept(conn, stream);
                            it = &this->polls_[i];

                            if (res.ok()) {
                                auto sconn = res.unwrap();
                                manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s: new stream %p was created using %p conn id=%zu",
                                    "openssl_quic", stream, it->desc.value.ssl, this->stream_id(sconn));
                                this->stream_processing(sconn);
                            }
                            else {
                                this->close_connection(conn, CLOSE_CONN_ERR);
                            }
                            it = &this->polls_[i];
                            continue;
                        }

                        break;
                    }

                    processed_event |= it->revents & (SSL_POLL_EVENT_ISU|SSL_POLL_EVENT_ISB);
                }

                /* new outcoming stream */
                if (it->revents & (SSL_POLL_EVENT_OSB) ||
                    it->revents & SSL_POLL_EVENT_OSU) {

                    processed_event |= it->revents & (SSL_POLL_EVENT_OSB|SSL_POLL_EVENT_OSU);
                }

                /* read stream error */
                if (it->revents & SSL_POLL_EVENT_ER) {
                    manapi_log_trace(debug::LOG_TRACE_LOW, "%s: %s in %p", "openssl_quic", "stream read error", it->desc.value.ssl);

                    auto data = SSL_get_app_data(it->desc.value.ssl);

                    if (data) {
                        auto sdata = *static_cast<shared_conn *> (data);
                        assert(sdata->wrk.flags & WRK_INTERFACE_IS_STREAM);
                        this->close_stream(sdata, CLOSE_CONN_ERR);
                    }
                    it = &this->polls_[i];
                    processed_event |= SSL_POLL_EVENT_ER;
                }

                /* write stream error */
                if (it->revents & SSL_POLL_EVENT_EW) {

                    auto data = SSL_get_app_data(it->desc.value.ssl);

                    if (data) {
                        auto sdata = *static_cast<shared_conn *> (data);
                        manapi_log_trace(debug::LOG_TRACE_LOW, "%s: %s in %p stream=%zu", "openssl_quic", "stream write error",
                            it->desc.value.ssl, this->stream_id(sdata));
                        assert(sdata->wrk.flags & WRK_INTERFACE_IS_STREAM);
                        this->close_stream(sdata, CLOSE_CONN_ERR);
                    }
                    it = &this->polls_[i];
                    processed_event |= SSL_POLL_EVENT_EW;
                }

                /* read stream */
                if (it->revents & SSL_POLL_EVENT_R) {

                    auto data = SSL_get_app_data(it->desc.value.ssl);

                    if (data) {
                        auto sdata = *static_cast<shared_conn *> (data);
                        assert(sdata->wrk.flags & WRK_INTERFACE_IS_STREAM);

                        auto sn = sdata->as<quic_stream_t>();
                        if (!(sn->flags & ev::DISCONNECT))
                            this->stream_processing(sdata);
                    }
                    it = &this->polls_[i];
                    processed_event |= SSL_POLL_EVENT_R;
                }

                /* write stream */
                if (it->revents & SSL_POLL_EVENT_W) {

                    auto data = SSL_get_app_data(it->desc.value.ssl);

                    if (data) {
                        auto sdata = *static_cast<shared_conn *> (data);
                        assert(sdata->wrk.flags & WRK_INTERFACE_IS_STREAM);
                        auto sn = sdata->as<quic_stream_t>();
                        if (!(sn->flags & ev::DISCONNECT))
                            this->flush_write_(sdata, sn);
                    }
                    it = &this->polls_[i];
                    processed_event |= SSL_POLL_EVENT_W;
                }


                /* the connection begins terminating */
                if (it->revents & SSL_POLL_EVENT_EC) {
                    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s: revents & SSL_POLL_EVENT_EC in %p conn",
                        "openssl_quic", it->desc.value.ssl);
                    auto conn_by_ip = this->conns_.find(std::string_view(arr.data(), arr.size()));
                    if (conn_by_ip != this->conns_.end()) {
                        auto cit = conn_by_ip->second.find(reinterpret_cast<std::uintptr_t>(it->desc.value.ssl));
                        if (cit != conn_by_ip->second.end()) {
                            this->close_connection(cit->second, CLOSE_CONN_SHUTDOWN);
                        }
                    }
                    it = &this->polls_[i];
                    processed_event |= SSL_POLL_EVENT_EC;
                }

                /* the connection is terminated */
                if (it->revents & SSL_POLL_EVENT_ECD) {
                    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "revents & SSL_POLL_EVENT_ECD in %p conn", it->desc.value.ssl);

                    auto conn_by_ip = this->conns_.find(std::string_view(arr.data(), arr.size()));
                    if (conn_by_ip != this->conns_.end()) {
                        auto cit = conn_by_ip->second.find(reinterpret_cast<std::uintptr_t>(it->desc.value.ssl));
                        if (cit != conn_by_ip->second.end()) {
                            this->close_connection(cit->second, CLOSE_CONN_EOF);
                        }
                    }
                    it = &this->polls_[i];
                    processed_event |= SSL_POLL_EVENT_ECD;
                }

                /* failure */
                if (it->revents & SSL_POLL_EVENT_F) {
                    /* ignore */
                    manapi_log_trace(debug::LOG_TRACE_HIGH, "%s: %s", "quic_openssl", "it->revents & SSL_POLL_EVENT_F");
                    processed_event |= SSL_POLL_EVENT_F;
                }

                /* listener error */
                if (it->revents & SSL_POLL_EVENT_EL) {
                    /* ignore */
                    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s: %s", "quic_openssl", "it->revents & SSL_POLL_EVENT_EL");
                    processed_event |= SSL_POLL_EVENT_EL;
                }

                i++;
            }
        }
    }
    else {
        ssl_dump_error_(SSL_get_error(MANAPI_AS_SSL(this->listener), rhs), "SSL_poll");

    }

    if (SSL_net_read_desired(MANAPI_AS_SSL(this->listener))) {
        if (!this->udp_accept_->is_active())
            this->udp_accept_->recv_start();
    }
    else {
        if (this->udp_accept_->is_active())
            this->udp_accept_->recv_stop();
    }

    if (SSL_net_write_desired(MANAPI_AS_SSL(this->listener))) {
        //this->bio_flush_write ();
    }



    if (this->t_->is_active()) {
        rhs = this->t_->stop();
    }

    if (!SSL_get_event_timeout(MANAPI_AS_SSL(this->listener), &tv, &isinfinite) && !isinfinite) {
        std::size_t const mil = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        rhs = this->t_->start(mil, 0, timeout_event_cb);

        if (rhs)
            manapi_log_trace("%s: %s failed due to %s", "openssl_quic", "timeout", ev::strerror(rhs));
    }

    this->bio_flush_write ();
}

void manapi::net::worker::openssl_quic::io_unbind_cb(ev::handle *s) MANAPIHTTP_NOEXCEPT {
    try {
        auto w = static_cast<openssl_quic *>(s->data);
        if (w) {
            w->finish_ref--;
            if (w->finish_ref)
                return;

            w->t_.reset();

            if (w->finish)
                w->udp::stop(std::move(w->finish));
        }
        else
            manapi_log_trace(debug::LOG_TRACE_HIGH, "%s: %s failed due to %s", "openssl_quic", "io_unbind_cb", "data is null");
    }
    catch (std::exception const &e) {
        manapi_log_error("%s: %s failed due to %s", "openssl_quic", "io_unbind_cb", e.what());
    }
}

manapi::error::status_or<manapi::net::worker::shared_conn> manapi::net::worker::openssl_quic::stream_accept(const shared_conn &conn, SSL *stream) MANAPIHTTP_NOEXCEPT {
    try {
        if (!conn->wrk.data) {
            /* it hasn't be initialized yet */
            if (this->try_init_conn_(conn))
                return error::status_internal("openssl_quic: conn hasn't be init-ed yet");
        }

        auto p = std::make_unique<quic_stream_t>();
        auto top = std::make_unique<connection_io>();

        auto stream_conn = std::shared_ptr<worker::connection> (
                new worker::connection{p.get()}, stream_interface_eraser);

        stream_conn->wrk.flags |= WRK_INTERFACE_IS_STREAM;
        stream_conn->version = conn->version;

        p.release();

        auto data = conn->as<quic_conn_t>();
        auto stream_data = stream_conn->as<quic_stream_t>();

        stream_data->top = std::move(top);
        stream_data->stream = stream;
        stream_data->parent = conn.get();

        auto rhs = data->streams.insert({SSL_get_stream_id(stream), stream_conn});
        if (!rhs.second) {
            SSL_free(stream);
            return error::status_internal("already exists - bug");
        }

        SSL_POLL_ITEM poll_item;
        poll_item.desc = SSL_as_poll_descriptor(stream);
        poll_item.events = SSL_POLL_EVENT_R|SSL_POLL_EVENT_W|SSL_POLL_EVENT_ER|SSL_POLL_EVENT_EW|SSL_POLL_EVENT_F;
        poll_item.revents = 0;
        this->polls_.push_back(poll_item);
        stream_data->poll_id = this->polls_.size() - 1;

        if (!SSL_set_app_data(stream, &rhs.first->second))
            return error::status_resource_exhausted();

        int init_res = 0;
        if (conn->wrk.data) {
            init_res = this->global_.init_stream_cb(conn, stream_conn, &this->global_, this);
        }
        else {
            init_res = this->global_.init_cb(stream_conn, &this->global_, this);
            this->waiting(conn, false);
        }

        if (init_res) {
            this->close_connection(stream_conn, CLOSE_CONN_ERR);
        }

        else {
            this->event_on(stream_conn,
                [this]
                (const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p) mutable
                -> void {
                    auto const wrk = this;
                    if (wrk->global_.accept_cb (conn, flags, buffer, nsize, p, &this->global_, this))
                        return;
            });

            this->event_flags(stream_conn, ev::READ);
            return stream_conn;
        }
    }
    catch (std::bad_alloc const &) {
        return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "openssl_quic:stream_accept", e.what());
    }

    return error::status_internal("openssl_quic:stream_accept");
}

void manapi::net::worker::openssl_quic::stream_processing(const shared_conn &s) MANAPIHTTP_NOEXCEPT {
    auto sn = s->as<quic_stream_t>();

    if (sn->flags & ev::DISCONNECT)
        return;

    this->flush_write_(s, sn);

    char buffer[16384];
    std::size_t readbytes;

    while (sn->flags & ev::READ) {
        ERR_clear_error();
        auto rhs = SSL_read_ex(sn->stream, buffer, sizeof (buffer), &readbytes);
        if (rhs == 1 && readbytes) {
            sn->transfered += readbytes;

            if ((s)->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
                this->global_.flush_custom_read_cb(s, &this->global_, this);
            else
                this->feed_event(s, ev::READ, buffer,
                    static_cast<ssize_t>(readbytes), nullptr);

            if (sn->flags & ev::DISCONNECT)
                return;

            continue;
        }

        auto err = SSL_get_error(sn->stream, rhs);

        switch (err) {
            case SSL_ERROR_WANT_READ:
                break;
            case SSL_ERROR_ZERO_RETURN: {
                if (!(sn->flags & CONN_RECV_END)) {
                    sn->flags |= CONN_RECV_END;
                    this->feed_event(s, CONN_RECV_END, buffer,
                                static_cast<ssize_t>(readbytes), nullptr);
                    if (sn->flags & ev::DISCONNECT)
                        return;
                }
                break;
            }
            default:
                this->close_connection(s, CLOSE_CONN_ERR);
                return;
        }

        break;
    }

    if (sn->flags & ev::WRITE && !sn->top->send_size)
        this->feed_event(s, ev::WRITE, nullptr, 0, nullptr);
}

manapi::error::status_or<manapi::net::worker::shared_conn> manapi::net::worker::openssl_quic::conn_accept(SSL *client, const sockaddr *addr) MANAPIHTTP_NOEXCEPT {
    shared_conn conn;
    quic_conn_t *sn;
    try {
        std::array<char, 17> arr;
        arr.fill('\0');
        auto p = std::make_unique<quic_conn_t>();
        auto ipstorage = std::make_unique<worker::connection::ipdata_t>();
        ipstorage->len = async::socklen(addr);
        memcpy (&ipstorage->client, addr, ipstorage->len);

        p->conn = client;
        p->worker = this;
        p->speed_min_delay = this->config_->speed_check_delay;
        p->flags = 0;

        auto shared = std::shared_ptr<worker::connection> (
            new worker::connection{p.get()}, connection_interface_eraser);

        sn = p.get();
        p.release();

        shared->ipdata = std::move(ipstorage);
        shared->ipdata->len = 0;

        memcpy(shared->ipdata->client.data, &this->config_->server_addr, this->config_->server_len);
        shared->ipdata->len = this->config_->server_len;

        const auto sock_data = reinterpret_cast <const sockaddr *>(shared->ipdata->client.data);
        *arr.data() = static_cast<char>(http::version_ip_by_addr (sock_data));
        http::ip_by_addr(sock_data, arr.data() + 1);

        auto conn_by_ip = this->conns_.find(std::string_view(arr.data(), arr.size()));
        if (conn_by_ip == this->conns_.end())
            conn_by_ip = this->conns_.insert({std::string(arr.data(), arr.size()), decltype(this->conns_)::value_type::second_type{}}).first;

        auto res = conn_by_ip->second.insert(
            {reinterpret_cast<std::uintptr_t>(client), std::move(shared)});

        if (!res.second) {
            SSL_free(client);
            return error::status_internal("openssl_quic: conn accept failed");
        }

        this->count++;
        this->worker_data_->as<http::server_ctx::worker_data_t>()->count.fetch_add(1);

        conn = res.first->second;

        this->waiting(conn, true);

        SSL_POLL_ITEM poll_item;
        poll_item.desc = SSL_as_poll_descriptor(client);
        poll_item.events = SSL_POLL_EVENT_F|SSL_POLL_EVENT_EC|SSL_POLL_EVENT_ECD|SSL_POLL_EVENT_ISB|
            SSL_POLL_EVENT_ISU|SSL_POLL_EVENT_OSB|SSL_POLL_EVENT_OSU;
        poll_item.revents = 0;
        this->polls_.push_back(poll_item);
        sn->poll_id = this->polls_.size() - 1;

        manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s: new connection %p", "openssl_quic", client);

        auto const version = static_cast<int>(reinterpret_cast<std::uintptr_t> (SSL_get_app_data(client)));
        if (version)
            conn->version = version;

        if (!SSL_set_app_data (client, &res.first->second)) {
            manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s: %s failed", "openssl_quic", "SSL_set_app_data");
            return error::status_internal("openssl_quic: conn accept failed");
        }

        if (conn_by_ip->second.size() > (this->config_->max_connections_by_ip + this->config_->max_connections_by_ip)
            || this->count > this->config_->max_connections + this->config_->max_connections) {
            this->close_connection(conn, CLOSE_CONN_EOF);
        }
        else if (conn_by_ip->second.size() > this->config_->max_connections_by_ip
            || this->count > this->config_->max_connections) {
            this->close_connection(conn, CLOSE_CONN_SHUTDOWN);
        }
    }
    catch (std::exception const &e) {
        if (!conn) {
            SSL_free(client);
        }

        manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s due to %s", "openssl_quic: conn accept failed", e.what());
        return error::status_internal("openssl_quic: conn accept failed");
    }

    if (!SSL_set_incoming_stream_policy(client, SSL_INCOMING_STREAM_POLICY_ACCEPT, 0))
        manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s: %s failed using %p conn", "openssl_quic", "SSL_set_incoming_stream_policy", client);

    conn_processing (sn->conn);

    return std::move(conn);
}

void manapi::net::worker::openssl_quic::conn_processing(SSL *client) MANAPIHTTP_NOEXCEPT {
    auto app_data = SSL_get_app_data(client);
    shared_conn conn;
    quic_conn_t *sn;

    assert (app_data);
    conn = *static_cast<shared_conn *> (app_data);
    sn = (conn)->as<quic_conn_t>();


    try {
        while (true) {
            if (sn->flags & CONN_QUIC_SHUTDOWN) {
                SSL_SHUTDOWN_EX_ARGS args{};
                args.quic_error_code = OSSL_QUIC_ERR_NO_ERROR;
                args.quic_reason = "timeout";
                auto rhs = SSL_shutdown_ex(sn->conn, 0, &args, sizeof (args));

                manapi_log_trace(debug::LOG_TRACE_LOW, "%s: %s %p returned %d", "openssl_quic", "SSL_shutdown()", sn->conn, rhs);
                auto err = SSL_get_error(sn->conn, rhs);
                if (err == SSL_ERROR_WANT_READ||
                    err == SSL_ERROR_WANT_WRITE) {
                    this->bio_flush_write();
                }
                else if (err) {
                    ssl_dump_error_(err, "SSL_shutdown");
                }

                if (rhs == 1) {
                    this->close_connection(conn, CLOSE_CONN_EOF);
                    break;
                }

                break;
            }

            if (SSL_is_init_finished(sn->conn)) {

            }
            else {
                auto rhs = SSL_do_handshake(sn->conn);

                manapi_log_trace(debug::LOG_TRACE_LOW, "%s: %s returned %d", "openssl_quic", "SSL_do_handshake()", rhs);


                if (SSL_is_init_finished(sn->conn)) {
                    if (this->try_init_conn_(conn))
                        goto err;
                }

                if (rhs > 0)
                    continue;

                auto const err = SSL_get_error(sn->conn, rhs);

                if (err == SSL_ERROR_WANT_READ ||
                    err == SSL_ERROR_WANT_WRITE) {
                    this->bio_flush_write();
                }
                else if (err)
                    ssl_dump_error_(err, "SSL_do_handshake()");

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

int manapi::net::worker::openssl_quic::try_init_conn_(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    if (!conn->wrk.data) {
        if (this->event_flags(conn) & (CONN_CLOSED))
            return ERR_INTERNAL;

        auto flags = this->global_.flags_cb(conn, &this->global_, this);

        if (flags & WRK_GLOBAL_FLAG_MULTISTREAM) {
            assert(!conn->wrk.data);
            try {
                if (this->global_.init_cb(conn, &this->global_, this))
                    return ERR_INTERNAL;
            }
            catch (std::exception const &e) {
                manapi_log_error("%s: %s failed due to %s", "openssl_quic", "setup connection", e.what());
                return ERR_INTERNAL;
            }
        }
    }

    return ERR_OK;
}


#endif
