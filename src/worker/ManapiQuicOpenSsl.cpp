#include "../include/worker/ManapiQuicOpenSsl.hpp"

#include <memory>

#include "ManapiString.hpp"

#ifdef MANAPIHTTP_OPENSSL_QUIC_SUPPORT

#   include <openssl/ssl.h>
#   include <openssl/quic.h>

manapi::net::worker::openssl_quic::openssl_quic(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config)
    : udp (std::move(site), std::move(wdata), config) {
    this->listener = nullptr;
    this->ctx = nullptr;
}

manapi::net::worker::openssl_quic::~openssl_quic() {

}

std::shared_ptr<manapi::net::worker::openssl_quic> manapi::net::worker::openssl_quic::create(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::openssl_quic>(std::move(site), std::move(wdata), config.get());
    worker->self_ = worker;
    return std::move(worker);
}

void manapi::net::worker::openssl_quic::onrecv(const std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) {

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

        w->alpn_ossltest(generate_alpn_ossltest(list));
    }

    SSL_CTX_set_verify(ctx, verify_peer, nullptr);
    SSL_CTX_set_alpn_select_cb(ctx, select_alpn, w);

    return manapi::error::status_ok();
}

manapi::error::status manapi::net::worker::openssl_quic::init(std::size_t deep) {
    using ci = internal::config_interface;

    auto res = udp::init(deep + 1);
    if (!res)
        return std::move(res);

    auto const ctx = SSL_CTX_new(OSSL_QUIC_server_method());
    if (!ctx)
        return error::status_internal("openssl_quic:SSL_CTX_new failed");

    auto sslconfig = ci::get_config_object_param(this->config_, "ssl", {});

    auto const sslcert = ci::get_config_param<std::string>(sslconfig, "cert", {});
    auto const sslkey = ci::get_config_param<std::string>(sslconfig, "key", {});

    res = load_certs(ctx, sslcert, sslkey);
    if (!res)
        return std::move(res);

    res = load_params(this, ctx, std::move(sslconfig));
    if (!res)
        return std::move(res);

    auto const listener = SSL_new_listener (ctx, 0);
    if (!listener)
        return error::status_internal("openssl_quic:SSL_new_listener failed");

    if (!SSL_set_blocking_mode(listener, 0))
        return error::status_internal("openssl_quic:SSL_set_blocking_mode failed");

    if (!SSL_listen(listener))
        return error::status_internal("openssl_quic:SSL_listen failed");



    return error::status_ok();
}

void manapi::net::worker::openssl_quic::stop(std::function<void()> cb) {
    udp::stop(std::move(cb));

}

void manapi::net::worker::openssl_quic::close_connection(shared_conn conn, int flags) {
}

int manapi::net::worker::openssl_quic::event_flags(const shared_conn &conn) {
}

int manapi::net::worker::openssl_quic::event_flags(const shared_conn &conn, int flags) {
}

std::unique_ptr<manapi::net::worker::worker_watcher_cb> manapi::net::worker::openssl_quic::event_on(
    const shared_conn &conn, std::unique_ptr<worker_watcher_cb> callback) {
}

void manapi::net::worker::openssl_quic::feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size,
    ibuffpool_t *p) {
}

bool manapi::net::worker::openssl_quic::is_valid_connection(worker::connection *connection) {
}

bool manapi::net::worker::openssl_quic::is_writable(const shared_conn &conn) {
}

std::size_t manapi::net::worker::openssl_quic::recv_count(const shared_conn &conn) const {
}

manapi::bytebuffer manapi::net::worker::openssl_quic::recv_first_buffer(const shared_conn &conn) {
}

ssize_t manapi::net::worker::openssl_quic::sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff,
    bool finish) {
}

ssize_t manapi::net::worker::openssl_quic::sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff,
    ssize_t size, bool finish, int maxcnt) {
}

void manapi::net::worker::openssl_quic::waiting(const shared_conn &conn, bool state) {
}

std::string_view manapi::net::worker::openssl_quic::alpn_ossltest() {
    return this->alpn_ossltest_;
}

void manapi::net::worker::openssl_quic::alpn_ossltest(std::string test) {
    this->alpn_ossltest_ = std::move(test);
}


#endif
