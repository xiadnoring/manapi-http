#include <memory>
#include <array>

#include "ManapiEventLoop.hpp"
#include "ManapiTimerPool.hpp"
#include "ManapiThreadPool.hpp"
#include "ManapiProcess.hpp"
#include "ManapiString.hpp"
#include "ManapiDns.hpp"
#include "http/ManapiHttpUtils.hpp"
#include "worker/ManapiBaseUtils.hpp"
#include "std/ManapiSocket.hpp"
#include "std/ManapiEasyCancelToken.hpp"
#include "../include/worker/ManapiQuicOpenSsl.hpp"

#ifdef MANAPIHTTP_OPENSSL_QUIC_SUPPORT

#   include <openssl/ssl.h>
#   include <openssl/quic.h>
#   include <openssl/err.h>

#   define SPEED_LIMIT_DEFAULT 10485760
#   define SPEED_LIMIT_INIT 10485760
#   define SPEED_LIMIT_STEP 10240000


#   define MANAPI_AS_BIO(n) static_cast<BIO*>(n)
#   define MANAPI_AS_SSL(n) static_cast<SSL*>(n)
#   define MANAPI_AS_CTX(n_) static_cast<SSL_CTX*>(n_)
#   define MANAPI_AS_BIO_ADDR(n_) (BIO_ADDR*)(n_)

#   define DATA_SIZE_PARTBYTE 4096
#   define DATA_SIZE_TOPBYTE (131072)
#   define QUIC_SSL_EVENT_REMOVED 1<<63

static int ssl_session_ctx_id = 1;

enum conn_tls_flags  {
    CONN_QUIC_SHUTDOWN = manapi::net::worker::base::CONN_MAX_CODE << 1,
    CONN_QUIC_EARLY_DATA = manapi::net::worker::base::CONN_MAX_CODE << 2,
    CONN_QUIC_EARLY_FINISHED = manapi::net::worker::base::CONN_MAX_CODE << 3,
    CONN_QUIC_SHUTDOWN_FINISHED = manapi::net::worker::base::CONN_MAX_CODE << 4,
    CONN_QUIC_IS_CLOSING = manapi::net::worker::base::CONN_MAX_CODE << 5,
    CONN_QUIC_CUSTOM_SHUTDOWN_FIN = manapi::net::worker::base::CONN_MAX_CODE << 6,
};

enum quic_openssl_worker_flags {
    CONN_QUIC_WORKER_POLL_LOOP = manapi::net::worker::WORKER_BASE_FLAG_MAX<<1
};

struct openssl_quic_worker_ctx_t {
    // std::unordered_map<std::string, SSL_SESSION*, manapi::text_hash, std::equal_to<>> sessions;
    SSL_CTX *ctx;
    manapi::timer sessions_flush_timer;
};

struct openssl_quic_bio_deleter_t {
    void operator() (BIO *b) const MANAPIHTTP_NOEXCEPT {
        BIO_free(b);
    }
};

struct openssl_quic_ctx_deleter_t {
    void operator() (SSL_CTX *b) const MANAPIHTTP_NOEXCEPT {
        SSL_CTX_free(b);
    }
};

struct openssl_quic_bio_addr_deleter {
    void operator() (BIO_ADDR *b) const MANAPIHTTP_NOEXCEPT {
        BIO_ADDR_free(b);
    }
};

struct openssl_quic_addrinfofree_deleter {
    void operator () (addrinfo *n) const MANAPIHTTP_NOEXCEPT {
        manapi::ev::getaddrinfo::free(n);
    }
};

struct manapi::net::worker::openssl_quic::quic_conn_t : connection_base2_t {
    SSL *conn;
    openssl_quic *worker;
    std::unordered_map<int64_t, shared_conn> streams;
    std::size_t streams_size;
    std::size_t poll_id;
};

struct manapi::net::worker::openssl_quic::quic_stream_t : connection_prepared_t {
    SSL *stream;
    worker::connection *parent;
    std::size_t poll_id;
    std::size_t cur_speed_lim;
};

manapi::net::worker::openssl_quic::openssl_quic(std::shared_ptr<net::worker::base_http> site, manapi::net::worker::worker_data_t* wdata, manapi::net::http::config *config)
    : udp(std::move(site), (wdata), config) {
    this->listener = nullptr;
    this->ctx = nullptr;
    this->flags_ |= WORKER_BASE_FLAG_MULTISTREAM|WORKER_BASE_FLAG_AUTO_ACK;
    this->pool_data_ = nullptr;
    this->deep_worker_id_ = 0;
    this->wbio = nullptr;
    this->rbio = nullptr;
    this->current_poll_id = 0;
    this->current_poll = nullptr;
}

manapi::net::worker::openssl_quic::~openssl_quic() {
    SSL_free(MANAPI_AS_SSL(this->listener));

    if (this->t_) {
        if (auto rhs = this->t_->stop()) {
            manapi_log_error (ev::strerror(rhs));
        }

        this->m_token.unref();
        this->t_->data( this->t_.get() );
        this->t_->unbind(+[] (manapi::ev::handle *s)
            -> void { delete static_cast<manapi::ev::timer *>(s->data); });
        this->t_.release();
    }

    if (this->update_limit_timer) {
        this->update_limit_timer.stop();
        this->update_limit_timer = nullptr;
    }

    if (this->pool_data_) {
        std::lock_guard<std::mutex> lk (this->pool_data_->mx);
        auto &wdata = this->pool_data_->data[this->deep_worker_id_];
        if (wdata.ref) {
            if (!(--wdata.ref)) {
                auto ctx_data = static_cast<openssl_quic_worker_ctx_t *> (wdata.data);
                ctx_data->sessions_flush_timer.stop();
                SSL_CTX_free(ctx_data->ctx);
                delete ctx_data;
                wdata.data = nullptr;
            }
        }
    }
}

std::shared_ptr<manapi::net::worker::openssl_quic> manapi::net::worker::openssl_quic::create(std::shared_ptr<net::worker::base_http> site, manapi::net::worker::worker_data_t* wdata, manapi::net::http::config* config) {
    auto worker = std::make_shared<worker::openssl_quic>(std::move(site), std::move(wdata), config);
    return std::move(worker);
}

static void ssl_dump_error_ (int err, const char *msg) {


    std::unique_ptr<BIO, openssl_quic_bio_deleter_t> bio;
    bio.reset(BIO_new(BIO_s_mem()));
    ERR_print_errors(bio.get());
    char *buf;
    ssize_t len = BIO_get_mem_data(bio.get(), &buf); assert(len >= 0);
    if (len && buf[len-1] == '\n')
        len--;

    manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "%s: %s err=%d, %.*s", "openssl_quic", msg, err, len, buf);
}

manapi::status load_certs (SSL_CTX *ctx, std::string_view cert, std::string_view key) {
    if (SSL_CTX_use_certificate_chain_file(ctx, cert.data()) <= 0) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %.*s", "openssl_quic:couldn't load certificate file",
            cert.size(), cert.data());
        return manapi::status_internal("openssl_quic:couldn't load certificate file");
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key.data(), SSL_FILETYPE_PEM) <= 0) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %.*s", "openssl_quic:couldn't load key file",
            key.size(), key.data());
        return manapi::status_internal("openssl_quic:couldn't load key file");
    }
    if (!SSL_CTX_check_private_key(static_cast<SSL_CTX*>(ctx)))
        return manapi::status_failed_precondition("openssl_quic:Private key does not match the certificate public key");

    return manapi::status_ok();
}

int manapi::net::worker::openssl_quic::select_alpn(SSL *ssl, const unsigned char **out, unsigned char *out_len, const unsigned char *in, unsigned int in_len, void *arg) {
    auto const w = static_cast<manapi::net::worker::openssl_quic *>(arg);
    auto const alpn_ossltest = w->alpn_ossltest();
    if (SSL_select_next_proto((unsigned char **)out, out_len, reinterpret_cast<const uint8_t*>(alpn_ossltest.data()),
        static_cast<uint32_t>(alpn_ossltest.size()), in, in_len) == OPENSSL_NPN_NEGOTIATED) {
        manapi_log_trace(debug::LOG_TRACE_LOW, "%s: %p selected %.*s", "openssl_quic", ssl, *out_len, *out);
        if (w->global_.alpn_cb) {
            auto version = w->global_.alpn_cb( &w->global_, reinterpret_cast<char const *>(*out), *out_len, w);
            if (version >= 0) {
                auto conn = static_cast<shared_conn *>(SSL_get_app_data(ssl));
                if (conn) {
                    (*conn)->version = version;
                }
                else {
                    if (!SSL_set_app_data(ssl, reinterpret_cast<char*>(static_cast<ssize_t>(version))))
                        return SSL_TLSEXT_ERR_ALERT_FATAL;
                }
            }
        }

        return SSL_TLSEXT_ERR_OK;
    }
    return SSL_TLSEXT_ERR_ALERT_FATAL;
}

static std::string generate_alpn_ossltest (const std::vector<std::string_view> &tests) {
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

manapi::status manapi::net::worker::openssl_quic::load_params (manapi::net::worker::openssl_quic *w, SSL_CTX *ctx, manapi::json sslconfig) {
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

        return manapi::status_ok();
    }
err:
    std::unique_ptr<BIO, openssl_quic_bio_deleter_t> bio;
    bio.reset(BIO_new(BIO_s_mem()));
    ERR_print_errors(bio.get());
    char *buf;
    ssize_t len = BIO_get_mem_data(bio.get(), &buf);
    assert(len >= 0);
    manapi_log_error("%s due to %.*s", "openssl_tls:create context failed", len, buf);
    return status_internal("openssl_tls:create context failed");
}

manapi::future<manapi::status> manapi::net::worker::openssl_quic::init(std::size_t deep) {
    auto udp_res = co_await udp::init(deep + 1);
    if (!udp_res)
        co_return std::move(udp_res);

    try {
        using ci = internal::config_interface;

        this->polls_.reserve(16);
        this->deep_worker_id_ = deep;

        this->pool_data_ = this->worker_data_->pools[this->worker_pool_id_];
        std::lock_guard<std::mutex> lk (this->pool_data_->mx);

        if (this->pool_data_->data.size() <= deep)
            this->pool_data_->data.resize(deep + 1);

        if (!(this->pool_data_->data[deep].ref))
            this->pool_data_->data[deep].data = new openssl_quic_worker_ctx_t{};

        auto ctx_data = static_cast<openssl_quic_worker_ctx_t *>(this->pool_data_->data[deep].data);
        this->pool_data_->data[deep].ref++;

        if (!ctx_data->sessions_flush_timer) {

        }
        if (!ctx_data->ctx) {
            std::unique_ptr<SSL_CTX, openssl_quic_ctx_deleter_t> ctx (SSL_CTX_new(OSSL_QUIC_server_method()));
            if (!ctx)
                co_return status_internal("openssl_quic:SSL_CTX_new");

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

        std::unique_ptr<BIO, openssl_quic_bio_deleter_t> wbio;
        std::unique_ptr<BIO, openssl_quic_bio_deleter_t> rbio;

        wbio.reset(BIO_new(BIO_s_dgram_mem()));
        rbio.reset(BIO_new(BIO_s_dgram_mem()));

        if (!wbio || !rbio) {
            co_return status_internal("openssl_quic:BIO_new");
        }
        auto const listener = SSL_new_listener (MANAPI_AS_CTX(this->ctx), 0);
        if (!listener)
            co_return status_internal("openssl_quic:SSL_new_listener");

        if (!SSL_set_blocking_mode(listener, 0)) {
            SSL_free(listener);
            co_return status_internal("openssl_quic:SSL_set_blocking_mode");
        }

        SSL_set_accept_state(listener);
        this->rbio = rbio.get();
        this->wbio = wbio.get();

        if (!BIO_dgram_set_caps(this->rbio, BIO_DGRAM_CAP_HANDLES_DST_ADDR|BIO_DGRAM_CAP_HANDLES_SRC_ADDR|BIO_DGRAM_CAP_PROVIDES_DST_ADDR|BIO_DGRAM_CAP_PROVIDES_SRC_ADDR)
            || !BIO_dgram_set_caps(this->wbio, BIO_DGRAM_CAP_HANDLES_DST_ADDR|BIO_DGRAM_CAP_HANDLES_SRC_ADDR|BIO_DGRAM_CAP_PROVIDES_DST_ADDR|BIO_DGRAM_CAP_PROVIDES_SRC_ADDR)) {
            SSL_free(listener);
            co_return status_internal("openssl_quic:BIO_dgram_set_caps");
        }

        if (!BIO_dgram_set_local_addr_enable(this->rbio, 1) ||
            !BIO_dgram_set_local_addr_enable(this->wbio, 1)) {
            SSL_free(listener);
            co_return status_internal("openssl_quic:BIO_dgram_set_local_addr_enable");
        }

        BIO_ADDR *local_addr = BIO_ADDR_new();
        if (!local_addr) {
            SSL_free(listener);
            co_return status_internal("openssl_quic:failed");
        }

        const sockaddr *addr = reinterpret_cast<const sockaddr *>(&this->config_->server_addr);
        int family = addr->sa_family;

        const void *raw_addr = nullptr;
        size_t raw_addr_len = 0;
        unsigned short port = 0;

        if (family == AF_INET) {
            const auto *addr_in = reinterpret_cast<const sockaddr_in *>(addr);
            raw_addr = &addr_in->sin_addr;      // IP (4 bytes)
            raw_addr_len = sizeof(addr_in->sin_addr);
            port = ntohs(addr_in->sin_port);
        }
        else if (family == AF_INET6) {
            const auto *addr_in6 = reinterpret_cast<const sockaddr_in6 *>(addr);
            raw_addr = &addr_in6->sin6_addr;    // IP (16 bytes)
            raw_addr_len = sizeof(addr_in6->sin6_addr);
            port = ntohs(addr_in6->sin6_port);
        }

        if (!BIO_ADDR_rawmake(local_addr, family, raw_addr, raw_addr_len, port)) {
            BIO_ADDR_free(local_addr);
            SSL_free(listener);
            co_return status_internal("openssl_quic:BIO_ADDR_rawmake failed");
        }

        BIO_ADDR *local_addr2 = BIO_ADDR_dup(local_addr);
        if (!local_addr2) {
            BIO_ADDR_free(local_addr);
            SSL_free(listener);
            co_return status_internal("openssl_quic:BIO_ADDR_dup failed");
        }

        if (!BIO_dgram_set0_local_addr(this->wbio, local_addr)) {
            BIO_ADDR_free(local_addr);
            BIO_ADDR_free(local_addr2);
            SSL_free(listener);
            co_return status_internal("openssl_quic:BIO_dgram_set0_local_addr");
        }

        if (!BIO_dgram_set0_local_addr(this->rbio, local_addr2)) {
            BIO_ADDR_free(local_addr2);
            SSL_free(listener);
            co_return status_internal("openssl_quic:BIO_dgram_set0_local_addr");
        }

        SSL_set_bio(listener, rbio.release(), wbio.release());

        if (!SSL_listen(listener)) {
            SSL_free(listener);
            co_return status_internal("openssl_quic:SSL_listen");
        }

        this->listener = listener;

        auto loop = manapi::async::current()->eventloop()->loop();
        if (!loop)
            co_return status_not_found("ev:loop");

        this->t_ = std::make_unique<ev::timer>();

        int rhs;

        if ((rhs = this->t_->bind(loop))) {
            this->t_.reset();
            manapi_log_trace("%s failed due to %s", "openssl_quic:uv timer bind", ev::strerror(rhs));
            co_return status_internal("openssl_quic:uv timer bind");
        }

        this->t_->data(this);
        this->m_token.ref();

        SSL_POLL_ITEM poll_item;

        poll_item.desc = SSL_as_poll_descriptor(listener);
        poll_item.events = SSL_POLL_EVENT_IC|SSL_POLL_EVENT_EL|SSL_POLL_EVENT_F;
        poll_item.revents = 0;

        this->polls_.push_back(poll_item);

        this->update_limit_timer = manapi::async::current()->timerpool()->append_interval_sync(
            1000, manapi::TIMER_IMPORTANT, [this] (const manapi::timer &t) -> void {
                this->update_limit_rate();
            }).unwrap();

        if ((rhs = this->udp_accept_->recv_start())) {
            manapi_log_trace("%s failed due to %s", "openssl_quic:recv_start", ev::strerror(rhs));
            co_return status_internal("openssl_quic:recv_start");
        }

        co_return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "openssl_quic:init failed", e.what());
    }
    co_return status_internal("openssl_quic:init failed");
}

void manapi::net::worker::openssl_quic::stop(manapi::stoken token) {
    if (this->t_) {
        if (auto rhs = this->t_->stop()) {
            manapi_log_error (ev::strerror(rhs));
        }

        this->t_->unbind(io_unbind_cb);
    }

    udp::stop( token );
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

    s->flags |= CONN_QUIC_IS_CLOSING;

    if (!( s->flags & CONN_QUIC_CUSTOM_SHUTDOWN_FIN ) && this->global_.flags_cb(conn, &this->global_, this) & WRK_GLOBAL_FLAG_SHUTDOWN_SUPPORTED) {
        manapi_log_trace(debug::LOG_TRACE_LOW, "openssl_quic: custom shutdown on %p", s->conn);
        auto rhs = this->global_.shutdown_cb(conn, &this->global_, this, !(flags & CLOSE_CONN_SHUTDOWN));
        if (!rhs) {
            return;
        }

        // now in QUIC
        s->flags |= CONN_QUIC_CUSTOM_SHUTDOWN_FIN;
        s->speed_min_delay = this->config_->max_shutdown_time;

        flags = CLOSE_CONN_SHUTDOWN;
    }

    if (flags & CLOSE_CONN_EOS) {
        s->flags |= CONN_QUIC_SHUTDOWN_FINISHED;
    }
    else {
        if ((s->flags & CONN_QUIC_SHUTDOWN && !(s->flags & CONN_QUIC_SHUTDOWN_FINISHED))) {
            this->conn_processing(s->conn);
            return;
        }
    }

    manapi_log_trace(debug::LOG_TRACE_LOW, "openssl_quic: rst all streams on %p (%zu)", s->conn, s->streams_size);

    for (auto it = s->streams.begin(); it != s->streams.end(); ++it) {
        if (!it->second)
            continue;
        this->rst_stream(it->second);
    }

    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "openssl_quic:close_connection() %p flags=%d", s->conn, flags);

    //prepared::timer_clear(std::move(s->timeout));

    if (!(s->flags & (CONN_QUIC_SHUTDOWN|CONN_QUIC_SHUTDOWN_FINISHED))) {
        s->flags |= CONN_QUIC_SHUTDOWN;

        this->conn_processing(s->conn);
        this->bio_flush_write();

        return;
    }

    if (s->streams_size) {
        return;
    }

    s->flags |= CONN_REMOVED|CONN_CLOSED;

    // MANAPIHTTP_MUST_ALLOC_START
    // manapi::async::current()->etaskpool()->append_static_task([conn] () -> void {
        std::array<char, 17> arr;
        memset(arr.data(), '\0', arr.size());
        auto const addr = reinterpret_cast<sockaddr *> (conn->ipdata->client.data);
        *arr.data() = static_cast<char>(http::version_ip_by_addr (addr));
        http::ip_by_addr(addr, arr.data() + 1);
        // auto s = conn->as<quic_conn_t>();
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
                cit->second = nullptr;
        }

        if (!SSL_set_app_data(s->conn, nullptr)) {
            /* who cares */
            manapi_log_trace(manapi::debug::LOG_TRACE_MEDIUM, "SSL_set_app_data(s->conn, nullptr) failed");
        }

        w->bio_flush_write();
    // });
    // MANAPIHTTP_MUST_ALLOC_END
}

int manapi::net::worker::openssl_quic::event_flags(const shared_conn &conn) MANAPIHTTP_NOEXCEPT {
    return prepared::event_flags(conn);
}

int manapi::net::worker::openssl_quic::event_flags(const shared_conn &conn, int flags) MANAPIHTTP_NOEXCEPT {
    auto const data = conn->as<quic_stream_t>();

    MANAPIHTTP_WORKER_EVENT_LOOP_STREAM(data) {
        if (data->ev_callback) {
            if (status & ev::DISCONNECT) {
                manapi::net::worker::openssl_quic::call_user_callback(&data->ev_callback, conn, CONN_CLOSED, nullptr, 0, nullptr);
            }
            else if ((status & ev::READ)) {
                if (conn->wrk.flags & WRK_INTERFACE_CUSTOM_READ)
                    this->global_.flush_custom_read_cb(conn, &this->global_, this);

                this->flush_read_ (conn, data);

                if (status & CONN_RECV_END) {
                    if (manapi::net::worker::openssl_quic::call_user_callback(&data->ev_callback, conn, CONN_RECV_END, nullptr, 0, nullptr))
                        this->close_connection(conn, CLOSE_CONN_ERR);
                }
            }
        }

        if (status & ev::WRITE) {
            this->feed_event(conn, ev::WRITE, nullptr, 0, nullptr);
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

void manapi::net::worker::openssl_quic::feed_event(const shared_conn &conn, int flags, const char *buff, std::size_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT {
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
    auto p = conn->as<quic_stream_t>();
    return prepared::sync_write(this, conn, p, buff, nbuff, p->cur_speed_lim, finish);
}

ssize_t manapi::net::worker::openssl_quic::sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, std::size_t size, bool finish, std::size_t maxcnt) MANAPIHTTP_NOEXCEPT {
    auto const s = conn->as<quic_stream_t>();
    ssize_t res = 0;

    if (s->flags & ev::DISCONNECT)
        return -1;


    if (s->flags & CONN_SEND_END)
        return -1;

    this->bio_flush_write();

    if (prepared::write_buffs_is_full(s->top.get(), maxcnt))
        return 0;

    while (nbuff) {
        std::size_t flags = 0;

        std::size_t written = 0;
        int rhs;

        if (s->top->send_size) {
            this->flush_write_(conn, s);
        }

        if (finish && nbuff == 1) {
            flags |= SSL_WRITE_FLAG_CONCLUDE;
            if (!s->top->send_size)
                manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "SSL_write_ex2 with SSL_WRITE_FLAG_CONCLUDE");
        }

        if (s->top->send_size) {
            rhs = 1;
            written = 0;
        }
        else {
            ERR_clear_error();
            rhs = SSL_write_ex2(s->stream, buff->base, buff->len, flags, &written);
        }

        if (rhs!=1) {
            assert(!written);
            auto err = SSL_get_error(s->stream, rhs);
            switch (err) {
                case SSL_ERROR_WANT_READ:
                case SSL_ERROR_WANT_WRITE:
                break;
                default: {
                    auto bioerr = ERR_peek_error();
                    if (bioerr && !BIO_err_is_non_fatal(static_cast<uint32_t>(bioerr))) {
                        ssl_dump_error_(err, "SSL_write_ex2");
                        ERR_clear_error();
                        return -1;
                    }
                    ERR_clear_error();
                    break;
                }
            }
            this->bio_flush_write();
        }

        s->transfered += written;

        if (!written) {
            auto sent = interface_worker::connection_io_send(&s->top->send, buff->base, (buff->len),
                &this->bufferpool(), this->config_->buffer_size, &s->top->send_size, maxcnt);

            if (sent < 0)
                return -1;

            res += sent;

            if (static_cast<std::size_t>(sent) == buff->len) {
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

    this->waiting(s, true);

    if (data->flags & CONN_REMOVED)
        return;

    if (flags & CLOSE_CONN_EOR || flags & CLOSE_CONN_EOS) {

    }
    else if (data->flags & CONN_WANT_CLOSE) {
        if (data->ev_callback &&
            !(manapi::net::worker::openssl_quic::call_user_callback(&data->ev_callback, s, CONN_WANT_CLOSE, nullptr, 0, nullptr))) {
        if (data->flags & CONN_WANT_CLOSE)
            return;
        }
    }

    auto stream_state = SSL_get_stream_write_state(data->stream);
    manapi_log_trace( manapi::debug::LOG_TRACE_LOW, "openssl_quic: write status is %d id=%lld", stream_state,
        (int64_t)SSL_get_stream_id(data->stream));

    s->cancellation.cancel();

    data->flags |= CONN_CLOSED;

    if (stream_state == SSL_STREAM_STATE_OK) {
        if (flags & (CLOSE_CONN_EOS|CLOSE_CONN_ERR)) {
            manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "openssl_quic: force close id=%lld",
                (int64_t)SSL_get_stream_id(data->stream));
        }
        else {
            return;
        }
    }


    data->flags |= CONN_REMOVED;

    if (!(flags & CLOSE_CONN_SHUTDOWN) && !(flags & CLOSE_CONN_FINISHED)) {
         manapi_log_trace(debug::LOG_TRACE_LOW, "openssl_quic: SSL_stream_reset on %p %llu", data->stream, static_cast<int64_t>(SSL_get_stream_id(data->stream)));
         SSL_STREAM_RESET_ARGS reset_args{};
         reset_args.quic_error_code = OSSL_QUIC_ERR_NO_ERROR;
         auto stream_status = SSL_stream_reset(data->stream, &reset_args, sizeof (reset_args));
         //if (stream_status) {
         ssl_dump_error_(SSL_get_error(data->stream, stream_status), "SSL_stream_reset");
    }


    this->bio_flush_write();

    prepared::event_callback_clear(s, data);

    prepared::top_buffer_clear(data);


    // MANAPIHTTP_MUST_ALLOC_START
    // manapi::async::current()->etaskpool()->append_static_task([s, flags] () mutable -> void {
    //     if (s) {
            // auto const data = s->as<quic_stream_t>();
            // if (!data)
            //     return;

            auto const conn_data = data->parent->as<quic_conn_t>();
            auto w = conn_data->worker;

            if (data->poll_id)
                w->remove_poll_id(std::exchange(data->poll_id, 0));

            auto const stream_id = static_cast<int64_t>(SSL_get_stream_id(data->stream));

            auto it = conn_data->streams.find(stream_id);

            if (it != conn_data->streams.end() && it->second) {
                // conn_data->streams.erase(it);
                conn_data->streams_size--;
                it->second = nullptr;
            }

            w->bio_flush_write();

            if (!conn_data->streams_size) {
                auto const app_data = SSL_get_app_data(conn_data->conn);
                if (app_data) {
                    // otherwise this connection is defected
                    auto conn = *static_cast<shared_conn *>(app_data);
                    w->waiting(conn, true);
                    if ((conn_data->flags & CONN_QUIC_IS_CLOSING)) {
                        w->close_connection(conn, CLOSE_CONN_SHUTDOWN);
                    }
                }
            }
    //     }
    // });
    // MANAPIHTTP_MUST_ALLOC_END
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

manapi::status_or<manapi::net::worker::shared_conn> manapi::net::worker::openssl_quic::new_stream(const shared_conn &conn, int flags) MANAPIHTTP_NOEXCEPT {
    try {
        if (!conn)
            goto args;

        auto const data = conn->as<quic_conn_t>();

        if (!data)
            goto args;


        std::size_t openssl_flags = 0;

        if (flags & CONN_STREAM_FLAG_UNI)
            openssl_flags |= SSL_STREAM_FLAG_UNI;

        auto stream = SSL_new_stream(data->conn, openssl_flags);
        if (!stream)
            return status_resource_exhausted();

        auto res = this->stream_accept(conn, stream, flags);
        if (!res.ok())
            return res.err();

        auto stream_conn = res.unwrap();
        auto const id = this->stream_id(stream_conn);

        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "openssl_quic: new local stream %p was created using %p id=%zu",
            stream, data->conn, id);



        return std::move(stream_conn);
    }
    catch (std::bad_alloc const &) {
        return status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "openssl_quic:new_stream", e.what());
        return status_internal("openssl_quic:new_stream");
    }
    args: return status_invalid_argument("new_stream");
}

int64_t manapi::net::worker::openssl_quic::stream_id(const shared_conn &s) MANAPIHTTP_NOEXCEPT {
    return static_cast<int64_t>(SSL_get_stream_id(s->as<quic_stream_t>()->stream));
}

manapi::net::worker::shared_conn manapi::net::worker::openssl_quic::stream_id(const shared_conn &conn, int64_t id) MANAPIHTTP_NOEXCEPT {
    assert(!(conn->wrk.flags & WRK_INTERFACE_IS_STREAM));
    auto const data = conn->as<quic_conn_t>();
    auto it = data->streams.find(id);
    if (it == data->streams.end() || !it->second)
        return nullptr;
    return it->second;
}

std::size_t manapi::net::worker::openssl_quic::streams_size(const shared_conn &conn) const MANAPIHTTP_NOEXCEPT {
    assert(conn && !(conn->wrk.flags & WRK_INTERFACE_IS_STREAM));
    return conn->as<quic_conn_t>()->streams_size;
}

void manapi::net::worker::openssl_quic::bio_flush_write() MANAPIHTTP_NOEXCEPT {
    ERR_clear_error();
    try {
        char buffer[4096];
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
                if (err && !BIO_err_is_non_fatal(static_cast<uint32_t>(err)))
                    ssl_dump_error_(SSL_get_error(MANAPI_AS_SSL(this->listener), rhs), "BIO_recvmmsg");
                ERR_clear_error();
                break;
            }

            for (std::size_t i = 0; i < msgs_processed; i++) {
                ev::buff_t buff;
                buff.base = static_cast<char *>(msg->data);
                buff.len = static_cast<decltype(buff.len)>(msg->data_len);

                rhs = this->udp_accept_->try_send(&buff, 1, reinterpret_cast<sockaddr *>(&storage_local));

                if (rhs != static_cast<ssize_t>(msg->data_len)) {
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
    if (this->flags_ & CONN_QUIC_WORKER_POLL_LOOP) {
        assert(this->polls_.size() > poll_id);

        this->polls_[poll_id].desc.value.ssl = nullptr;
        this->polls_[poll_id].revents = 0;
        this->polls_[poll_id].events = 0;
    }
    else {
        assert(this->polls_.size() > poll_id);

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

            if (!ssl)
                return;

            if (SSL_is_listener(ssl) || poll_id == this->polls_.size())
                return;

            auto data = static_cast<shared_conn*>(SSL_get_app_data(ssl));

            if (data) {
                // otherwise this connection is defected
                auto &conn = *data;
                if (conn->wrk.flags & WRK_INTERFACE_IS_STREAM) {
                    auto s = conn->as<quic_stream_t>();
                    assert(s->poll_id);
                    s->poll_id = poll_id;
                }
                else {
                    auto s = conn->as<quic_conn_t>();
                    assert(s->poll_id);
                    s->poll_id = poll_id;
                }
            }
        }
    }
}

void manapi::net::worker::openssl_quic::flush_read_(const shared_conn &conn, quic_stream_t *data) MANAPIHTTP_NOEXCEPT {
    return prepared::flush_read_(this, conn, data);
}

void manapi::net::worker::openssl_quic::flush_write_(const shared_conn &conn, quic_stream_t *data) MANAPIHTTP_NOEXCEPT {
    while (data->top->send.last_deque) {
        std::size_t flags = 0;
        bool const last = data->top->send.deque.get() == data->top->send.last_deque;

        if (data->flags & CONN_SEND_END && last) {
            flags |= SSL_WRITE_FLAG_CONCLUDE;

            manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "SSL_write_ex2 with SSL_WRITE_FLAG_CONCLUDE");
        }

        auto &buffer = data->top->send.deque->buffer;

        // if (data->top->send.deque_current)
        //     buffer.shift_add(std::exchange(data->top->send.deque_current, 0));

        std::size_t size;

        if (last)
            size = data->top->send.deque_cursor - data->top->send.deque_current;
        else
            size = buffer.size() - data->top->send.deque_current;

        std::size_t written;

        if (size) {
            ERR_clear_error();
            auto rhs = SSL_write_ex2(data->stream, buffer.data() + data->top->send.deque_current, size, flags, &written);

            if (rhs!=1) {
                assert(!written);
                auto err = SSL_get_error(data->stream, rhs);
                switch (err) {
                    case SSL_ERROR_WANT_READ:
                    case SSL_ERROR_WANT_WRITE:

                    break;
                    default: {
                        auto bioerr = ERR_peek_error();
                        if (bioerr && !BIO_err_is_non_fatal(static_cast<uint32_t>(bioerr)))
                            ssl_dump_error_(err, "SSL_write_ex2");

                        ERR_clear_error();

                        this->close_stream(conn, CLOSE_CONN_EOS);

                        break;
                    }
                }

                this->bio_flush_write();
                break;
            }

            data->transfered += written;
        }
        else {
            written = 0;

            if (last) {
                // should be OK
                SSL_stream_conclude(data->stream, 0);
            }
        }

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

        data->top->send.deque_current += static_cast<uint32_t>(written);
        assert(data->top->send.deque_current <= buffer.size());

        break;
    }

    this->bio_flush_write();

    if ( !(data->flags & CONN_CLOSED) && !data->top->send_size)
        this->feed_event(conn, ev::WRITE, nullptr, 0, nullptr);
}

void manapi::net::worker::openssl_quic::update_limit_rate() MANAPIHTTP_NOEXCEPT {
    this->bio_flush_write ();

    /* in event loop */
    for (auto nit = this->conns_.begin(); nit != this->conns_.end(); ) {
        auto &conns = nit->second;
        if (conns.empty()) {
            nit = this->conns_.erase(nit);
            continue;
        }

        for (auto cit = conns.begin(); cit != conns.end(); ) {
            if (!cit->second) {
                cit = conns.erase(cit);
                continue;
            }

            auto conn = cit->second;
            this->update_limit_rate_connection(conn);
            ++cit;
        }

        ++nit;
    }
}

void manapi::net::worker::openssl_quic::update_limit_rate_connection(const shared_conn &sconn) MANAPIHTTP_NOEXCEPT {
    auto conn_data = sconn->as<quic_conn_t>();
    auto &config = this->config_;

    for (auto it = conn_data->streams.begin(); it != conn_data->streams.end(); ) {
        try {
            if (!it->second) {
                it = conn_data->streams.erase(it);
                continue;
            }

            auto data = it->second->as<quic_stream_t>();
            auto conn = it->second;

            conn_data->transfered += data->transfered;
            bool force_recall = false;

            if (data->transfered >= data->cur_speed_lim) {
                data->cur_speed_lim += std::max<std::size_t>(SPEED_LIMIT_STEP, data->cur_speed_lim / 2);
                force_recall = true;
            }
            else {
                auto const s = std::max<std::size_t>(data->transfered, SPEED_LIMIT_DEFAULT);
                data->cur_speed_lim = std::max<std::size_t>(s, config->speed_stream_check_bytes / config->speed_stream_check_delay);
            }

            if ((force_recall || data->transfered >= config->speed_limit_rate)
                && data->ev_callback) {
                data->transfered = 0;

                if (!(data->flags & ev::DISCONNECT) && data->flags & ev::WRITE && data->ev_callback) {
                    if (manapi::net::worker::base::call_user_callback(&data->ev_callback, conn, ev::WRITE, nullptr, 0, nullptr)) {
                        this->close_connection(conn, CLOSE_CONN_EOS);
                    }
                }
            }
            else {
                data->transfered_k += data->transfered;

                if (--data->speed_min_delay <= 0) {
                    if (!(it->second->wrk.flags & WRK_INTERFACE_IS_CTRL)) {
                        if (data->flags & (base::CONN_IO_WAITING) && (data->transfered_k < config->speed_stream_check_bytes)) {
                            if (data->flags & CONN_WAS_SHUTDOWN) {
                                data->speed_min_delay = this->config_->speed_stream_check_delay;
                                conn_data->worker->close_connection(conn, CLOSE_CONN_EOS);
                            }
                            else {
                                data->speed_min_delay = this->config_->max_shutdown_time;
                                conn_data->worker->close_connection(conn, CLOSE_CONN_SHUTDOWN);
                                data->flags |= CONN_WAS_SHUTDOWN;
                            }
                        }
                    }
                    else
                        data->speed_min_delay = (config->speed_stream_check_delay);
                    data->transfered_k = 0;
                }
                data->transfered = 0;
            }
        }
        catch (std::exception const &e) {
            manapi_log_error("%s: %s failed due to %s", "openssl_quic",
                "update_limit_rate_connection", e.what());
        }

        ++it;
    }

    conn_data->transfered_k += conn_data->transfered;


    if (--conn_data->speed_min_delay <= 0) {
        if (conn_data->flags & (base::CONN_IO_WAITING) && (conn_data->transfered_k < config->speed_check_bytes)) {
            if (conn_data->flags & CONN_WAS_SHUTDOWN) {
                conn_data->speed_min_delay = this->config_->speed_check_delay;
                this->close_connection(sconn, CLOSE_CONN_EOS);
            }
            else {
                conn_data->speed_min_delay = this->config_->max_shutdown_time;
                this->close_connection(sconn, CLOSE_CONN_SHUTDOWN);
                conn_data->flags |= CONN_WAS_SHUTDOWN;
            }
        }
        else
            conn_data->speed_min_delay = this->config_->speed_check_delay;
        conn_data->transfered_k = 0;
    }
    conn_data->transfered = 0;
}

void manapi::net::worker::openssl_quic::stream_interface_eraser(worker::connection *n) MANAPIHTTP_NOEXCEPT {
    if (!n)
        return;

    auto connection = std::unique_ptr<quic_stream_t> (n->as<quic_stream_t>());

    auto conn_data = connection->parent->as<quic_conn_t>();
    auto const conn_ptr = static_cast<shared_conn *>(SSL_get_app_data(conn_data->conn));
    shared_conn conn{nullptr};
    if (conn_ptr)
        conn = *conn_ptr;

    auto const wrk =  (conn_data->worker);


    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s:Free QUIC %p(%zu) stream using %p conn",
        "openssl_quic", connection.get(), SSL_get_stream_id(connection->stream), conn_data->conn);

    if (!SSL_set_app_data(connection->stream, nullptr)) {
        /* who cares */
    }

    SSL_free(connection->stream);

    /**
     * Decrease count of streams
     */
    if (wrk) {
        wrk->bio_flush_write();

        if (auto rhs = wrk->global_.cleanup_cb(n, &wrk->global_, wrk))
            manapi_log_error("%s: %s failed due to %d", "openssl_quic", "this->global_.cleanup_cb failed", rhs);

        if (!conn_data->streams_size && conn) {
            if (conn_data->flags & CONN_QUIC_IS_CLOSING) {
                wrk->close_connection(conn, CLOSE_CONN_SHUTDOWN);
            }
        }
    }
}

void manapi::net::worker::openssl_quic::connection_interface_eraser(worker::connection *n) MANAPIHTTP_NOEXCEPT {
    if (!n)
        return;

    auto connection = std::unique_ptr<quic_conn_t> (n->as<quic_conn_t>());

    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s:Free QUIC %p conn", "openssl_quic", connection.get());

    SSL_free(connection->conn);

    auto const wrk =  (connection->worker);
    if (wrk) {
        wrk->bio_flush_write();

        if (n->wrk.data) {
            if (auto rhs = wrk->global_.cleanup_cb(n, &wrk->global_, wrk))
                manapi_log_error("%s: %s failed due to %d", "openssl_quic", "this->global_.cleanup_cb failed", rhs);
        }

        wrk->worker_data()->count.fetch_sub(1);
        wrk->m_token.unref();
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
        msg.data = buff; assert(size >= 0);
        msg.data_len = static_cast<std::size_t>(size);
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

    rhs = SSL_poll(this->polls_.data(), this->polls_.size(), sizeof (SSL_POLL_ITEM), &poll_tv, SSL_POLL_FLAG_NO_HANDLE_EVENTS, &result);
    if (rhs) {
        auto &it = this->current_poll;
        if (result) {
            this->current_poll_id = 0;
            assert(!(this->flags_ & CONN_QUIC_WORKER_POLL_LOOP));
            this->flags_ |= CONN_QUIC_WORKER_POLL_LOOP;
            while (true) {
                if (this->current_poll_id >= this->polls_.size())
                    break;

                this->current_poll = &this->polls_[this->current_poll_id];

                std::size_t processed_event = 0;

                if (it->revents != SSL_POLL_EVENT_NONE) {
                    assert(it->desc.value.ssl);
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
                                assert (this->polls_[this->current_poll_id].desc.value.ssl == MANAPI_AS_SSL(this->listener));

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
                            if (!it->desc.value.ssl)
                                break;

                            auto conn = *static_cast<shared_conn *>(SSL_get_app_data(it->desc.value.ssl));
                            auto cdata = conn->as<quic_conn_t>();

                            auto stream = SSL_accept_stream(it->desc.value.ssl, 0);
                            if (stream) {
                                auto res = this->stream_accept(conn, stream, 0);

                                if (res.ok()) {
                                    auto sconn = res.unwrap();
                                    manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s: new stream %p was created using %p conn id=%zu",
                                        "openssl_quic", stream, it->desc.value.ssl, this->stream_id(sconn));
                                    this->stream_processing(sconn);
                                }
                                else {
                                    this->close_connection(conn, CLOSE_CONN_ERR);
                                }

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

                        auto data = SSL_get_app_data(it->desc.value.ssl);

                        if (data) {
                            auto sconn = *static_cast<shared_conn *> (data);
                            auto sdata = sconn->as<quic_stream_t>();
                            if (!(sdata->flags & CONN_RECV_END)) {
                                manapi_log_trace(debug::LOG_TRACE_LOW, "%s: %s in %p", "openssl_quic", "stream read error", it->desc.value.ssl);
                                assert(sconn->wrk.flags & WRK_INTERFACE_IS_STREAM);
                                // this->close_stream(sconn, CLOSE_CONN_ERR);

                                sdata->flags |= CONN_RECV_END;
                                this->feed_event(sconn, CONN_RECV_END, nullptr, 0, nullptr);
                            }
                        }
                        processed_event |= SSL_POLL_EVENT_ER;
                    }

                    /* write stream error */
                    if (it->revents & SSL_POLL_EVENT_EW) {

                        auto data = SSL_get_app_data(it->desc.value.ssl);

                        if (data) {
                            auto sconn = *static_cast<shared_conn *> (data);
                            auto sdata = sconn->as<quic_stream_t>();
                            if (!(sdata->flags & CONN_SEND_END)) {
                                manapi_log_trace(debug::LOG_TRACE_LOW, "%s: %s in %p stream=%zu", "openssl_quic", "stream write error",
                                    it->desc.value.ssl, this->stream_id(sconn));
                                assert(sconn->wrk.flags & WRK_INTERFACE_IS_STREAM);
                                // this->close_stream(sconn, CLOSE_CONN_ERR);

                                sdata->flags |= CONN_SEND_END;
                                this->feed_event(sconn, CONN_SEND_END, nullptr, 0, nullptr);
                            }
                        }
                        processed_event |= SSL_POLL_EVENT_EW;
                    }

                    /* read stream */
                    if (it->revents & SSL_POLL_EVENT_R) {
                        auto data = SSL_get_app_data(it->desc.value.ssl);

                        if (data) {
                            auto sconn = *static_cast<shared_conn *> (data);
                            assert(sconn->wrk.flags & WRK_INTERFACE_IS_STREAM);

                            auto sn = sconn->as<quic_stream_t>();

                            if (!(sn->flags & CONN_CLOSED)) {
                                assert(!(sn->flags & CONN_REMOVED));
                                this->stream_processing(sconn);
                            }
                        }
                        processed_event |= SSL_POLL_EVENT_R;
                    }

                    /* write stream */
                    if (it->revents & SSL_POLL_EVENT_W) {

                        auto data = SSL_get_app_data(it->desc.value.ssl);

                        if (data) {
                            auto sconn = *static_cast<shared_conn *> (data);
                            assert(sconn->wrk.flags & WRK_INTERFACE_IS_STREAM);
                            auto sn = sconn->as<quic_stream_t>();

                            this->flush_write_(sconn, sn);

                            if ((sn->flags & CONN_CLOSED)) {
                                auto stream_state = SSL_get_stream_write_state(sn->stream);
                                manapi_log_trace( manapi::debug::LOG_TRACE_LOW, "openssl_quic: write status is %d id=%lld", stream_state,
                                    (int64_t)SSL_get_stream_id(sn->stream));
                                if (stream_state != SSL_STREAM_STATE_OK)
                                    this->close_stream(sconn, CLOSE_CONN_FINISHED);
                            }
                        }
                        processed_event |= SSL_POLL_EVENT_W;
                    }


                    /* the connection begins terminating */
                    if (it->revents & SSL_POLL_EVENT_EC) {
                        auto data = SSL_get_app_data(it->desc.value.ssl);

                        if (data) {
                            auto sconn = *static_cast<shared_conn *> (data);
                            auto sdata = sconn->as<quic_conn_t>();
                            if (!(sdata->flags & CONN_QUIC_IS_CLOSING)) {
                                assert(!(sconn->wrk.flags & WRK_INTERFACE_IS_STREAM));
                                this->close_connection(sconn, CLOSE_CONN_SHUTDOWN);
                            }

                            // std::array<char, 17> arr;
                            // memset(arr.data(), '\0', arr.size());
                            // *arr.data() = static_cast<char>(http::version_ip_by_addr ( reinterpret_cast<sockaddr *>(&sconn->ipdata->client)));
                            // http::ip_by_addr(reinterpret_cast<sockaddr *>(&sconn->ipdata->client), arr.data() + 1);
                            //
                            // auto conn_by_ip = this->conns_.find(std::string_view(arr.data(), arr.size()));
                            // if (conn_by_ip != this->conns_.end()) {
                            //     auto cit = conn_by_ip->second.find(reinterpret_cast<std::uintptr_t>(it->desc.value.ssl));
                            //     if (cit != conn_by_ip->second.end() && cit->second) {
                            //         auto conn = cit->second;
                            //         auto sdata = cit->second->as<quic_conn_t>();
                            //         this->close_connection(cit->second, CLOSE_CONN_SHUTDOWN);
                            //     }
                            // }
                        }
                        processed_event |= SSL_POLL_EVENT_EC;
                    }

                    /* the connection is terminated */
                    if (it->revents & SSL_POLL_EVENT_ECD) {

                        auto data = SSL_get_app_data(it->desc.value.ssl);

                        if (data) {
                            auto sconn = *static_cast<shared_conn *> (data);
                            assert(!(sconn->wrk.flags & WRK_INTERFACE_IS_STREAM));
                            this->close_connection(sconn, CLOSE_CONN_EOS);

                            // std::array<char, 17> arr;
                            // memset(arr.data(), '\0', arr.size());
                            // *arr.data() = static_cast<char>(http::version_ip_by_addr ( reinterpret_cast<sockaddr *>(&sconn->ipdata->client)));
                            // http::ip_by_addr(reinterpret_cast<sockaddr *>(&sconn->ipdata->client), arr.data() + 1);
                            //
                            // manapi_log_trace(debug::LOG_TRACE_MEDIUM, "revents & SSL_POLL_EVENT_ECD in %p conn", it->desc.value.ssl);
                            //
                            // auto conn_by_ip = this->conns_.find(std::string_view(arr.data(), arr.size()));
                            // if (conn_by_ip != this->conns_.end()) {
                            //     auto cit = conn_by_ip->second.find(reinterpret_cast<std::uintptr_t>(it->desc.value.ssl));
                            //     if (cit != conn_by_ip->second.end() && cit->second) {
                            //         auto conn = cit->second;
                            //         auto sdata = conn->as<quic_conn_t>();
                            //
                            //         this->close_connection(cit->second, CLOSE_CONN_EOS);
                            //     }
                            // }
                        }
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

                }

                if (!it->desc.value.ssl) {
                    auto const prev = this->flags_;
                    this->flags_ ^= CONN_QUIC_WORKER_POLL_LOOP;
                    this->remove_poll_id(this->current_poll_id);
                    this->flags_ = prev;
                    continue;
                }

                // nullify processed events
                it->revents ^= processed_event;

                this->current_poll_id++;
            }
            this->flags_ ^= CONN_QUIC_WORKER_POLL_LOOP;
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

    if (SSL_get_event_timeout(MANAPI_AS_SSL(this->listener), &tv, &isinfinite) && !isinfinite) {
        std::size_t const mil = static_cast<std::size_t>(tv.tv_sec) * 1000 + static_cast<std::size_t>(tv.tv_usec) / 1000;
        rhs = this->t_->start(mil, 0, timeout_event_cb);

        if (rhs)
            manapi_log_trace("%s: %s failed due to %s", "openssl_quic", "timeout", ev::strerror(rhs));
    }

    this->bio_flush_write ();
}

void manapi::net::worker::openssl_quic::io_unbind_cb(ev::handle *s) MANAPIHTTP_NOEXCEPT {

    auto w = static_cast<openssl_quic *>(s->data);
    assert (!!w);

    w->t_.reset();
    w->m_token.unref();
}

manapi::status_or<manapi::net::worker::shared_conn> manapi::net::worker::openssl_quic::stream_accept(const shared_conn &conn, SSL *stream, int flags) MANAPIHTTP_NOEXCEPT {
    shared_conn stream_conn;
    try {
        if (!conn->wrk.data) {
            /* it hasn't be initialized yet */
            if (this->try_init_conn_(conn))
                return status_internal("openssl_quic: conn hasn't be init-ed yet");
        }

        auto p = std::make_unique<quic_stream_t>();
        auto top = std::make_unique<connection_io>();

        p->cur_speed_lim = std::max<std::size_t>(SPEED_LIMIT_INIT, this->config_->speed_stream_check_bytes
            / this->config_->speed_stream_check_delay);

        stream_conn = reference (
                new worker::connection(p.release(), stream_interface_eraser));

        if (flags & CONN_STREAM_FLAG_CTRL)
            stream_conn->wrk.flags |= WRK_INTERFACE_IS_CTRL;

        stream_conn->wrk.flags |= WRK_INTERFACE_IS_STREAM;
        stream_conn->version = conn->version;

        auto data = conn->as<quic_conn_t>();
        auto stream_data = stream_conn->as<quic_stream_t>();

        stream_data->top = std::move(top);
        stream_data->stream = stream;
        stream_data->parent = conn.get();

        auto rhs = data->streams.insert({SSL_get_stream_id(stream), stream_conn});
        if (!rhs.second) {
            return status_internal("already exists - bug");
        }

        data->streams_size++;

        if (!SSL_set_app_data(stream, &rhs.first->second)) {
            this->close_connection(stream_conn, CLOSE_CONN_ERR);
            return status_resource_exhausted();
        }

        SSL_POLL_ITEM poll_item;
        poll_item.desc = SSL_as_poll_descriptor(stream);
        poll_item.events = SSL_POLL_EVENT_R|SSL_POLL_EVENT_W|SSL_POLL_EVENT_ER|SSL_POLL_EVENT_EW|SSL_POLL_EVENT_F;
        poll_item.revents = 0;
        this->polls_.push_back(poll_item);
        if (this->flags_ & CONN_QUIC_WORKER_POLL_LOOP) {
            assert(this->polls_.size() > this->current_poll_id);
            this->current_poll = &this->polls_[this->current_poll_id];
        }
        stream_data->poll_id = this->polls_.size() - 1;


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
                (const worker::shared_conn &conn, int flags, const char *buffer, std::size_t nsize, ibuffpool_t *p) mutable
                -> void {
                    auto const wrk = this;
                    if (wrk->global_.accept_cb (conn, flags, buffer, nsize, p, &this->global_, this))
                        return;
            });

            this->event_flags(stream_conn, ev::READ);
        }

        return stream_conn;

    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "openssl_quic:stream_accept", e.what());

        if (stream_conn)
            this->close_connection(stream_conn, CLOSE_CONN_ERR);
        else {
            if (!this->polls_.empty() && this->polls_.back().desc.value.ssl == stream)
                this->polls_.pop_back();
            SSL_free(stream);
        }
    }

    return status_internal("openssl_quic:stream_accept");
}

void manapi::net::worker::openssl_quic::stream_processing(const shared_conn &s) MANAPIHTTP_NOEXCEPT {
    auto sn = s->as<quic_stream_t>();

    if (sn->flags & ev::DISCONNECT)
        return;

    // this->flush_write_(s, sn);

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
                    (readbytes), nullptr);

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
                                (readbytes), nullptr);
                    // if (sn->flags & ev::DISCONNECT)
                    //     return;
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

manapi::status_or<manapi::net::worker::shared_conn> manapi::net::worker::openssl_quic::conn_accept(SSL *client, const sockaddr *addr) MANAPIHTTP_NOEXCEPT {
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


        sn = p.get();
        conn = reference (
            new worker::connection(p.release(), connection_interface_eraser));

        conn->ipdata = std::move(ipstorage);
        conn->ipdata->len = 0;

        memcpy(conn->ipdata->client.data, &this->config_->server_addr, this->config_->server_len);
        conn->ipdata->len = this->config_->server_len;

        const auto sock_data = reinterpret_cast <const sockaddr *>(conn->ipdata->client.data);
        *arr.data() = static_cast<char>(http::version_ip_by_addr (sock_data));
        http::ip_by_addr(sock_data, arr.data() + 1);

        auto conn_by_ip = this->conns_.find(std::string_view(arr.data(), arr.size()));
        if (conn_by_ip == this->conns_.end())
            conn_by_ip = this->conns_.insert({std::string(arr.data(), arr.size()), decltype(this->conns_)::value_type::second_type{}}).first;

        auto res = conn_by_ip->second.insert_or_assign(
            reinterpret_cast<std::uintptr_t>(client), conn);

        if (!res.second) {
            // SSL_free(client);
            // return status_internal("openssl_quic: conn accept failed");
        }

        auto const count = this->worker_data_->count.fetch_add(1);

        this->waiting(conn, true);

        auto const version = static_cast<int>(reinterpret_cast<std::uintptr_t> (SSL_get_app_data(client)));
        if (version)
            conn->version = version;

        if (!SSL_set_app_data (client, &res.first->second)) {
            manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s: %s failed", "openssl_quic", "SSL_set_app_data");
            this->close_connection(conn, CLOSE_CONN_ERR);
            return status_internal("openssl_quic: conn accept failed");
        }

        SSL_POLL_ITEM poll_item;
        poll_item.desc = SSL_as_poll_descriptor(client);
        poll_item.events = SSL_POLL_EVENT_F|SSL_POLL_EVENT_EC|SSL_POLL_EVENT_ECD|SSL_POLL_EVENT_ISB|
            SSL_POLL_EVENT_ISU|SSL_POLL_EVENT_OSB|SSL_POLL_EVENT_OSU;
        poll_item.revents = 0;
        this->polls_.push_back(poll_item);
        if (this->flags_ & CONN_QUIC_WORKER_POLL_LOOP) {
            assert(this->polls_.size() > this->current_poll_id);
            this->current_poll = &this->polls_[this->current_poll_id];
        }
        sn->poll_id = this->polls_.size() - 1;

        manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s: new connection %p", "openssl_quic", client);


        if (conn_by_ip->second.size() > (this->config_->max_connections_by_ip + this->config_->max_connections_by_ip)
            || count > this->config_->max_connections + this->config_->max_connections) {
            this->close_connection(conn, CLOSE_CONN_ERR);
        }
        else if (conn_by_ip->second.size() > this->config_->max_connections_by_ip
            || count > this->config_->max_connections) {
            this->close_connection(conn, CLOSE_CONN_ERR);
        }
    }
    catch (std::exception const &e) {
        if (conn) {
            this->close_connection(conn, CLOSE_CONN_ERR);
        }
        else {
            if (!this->polls_.empty() && this->polls_.back().desc.value.ssl == client)
                this->polls_.pop_back();
            SSL_free(client);
        }

        manapi_log_trace(debug::LOG_TRACE_MEDIUM, "%s due to %s", "openssl_quic: conn accept failed", e.what());
        return status_internal("openssl_quic: conn accept failed");
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
                args.quic_reason = "shutdown";
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
                    this->close_connection(conn, CLOSE_CONN_EOS);
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
