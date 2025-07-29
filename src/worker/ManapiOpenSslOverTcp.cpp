#include "worker/ManapiOpenSslOverTcp.hpp"

#include "ManapiParams.hpp"
#include "async/ManapiAsyncSocket.hpp"
#include "ManapiInitTools.hpp"
#include "../include/ManapiUtils.hpp"
#include "../../include/worker/ManapiBaseUtils.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY

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
#include <set>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/engine.h>

#include "../include/ManapiUtils.hpp"

struct ssl_worker_ctx_t {
    std::map<std::string, SSL_SESSION*, std::less<>> sessions;
    SSL_CTX *ctx;
    manapi::timer sessions_flush_timer;
};

struct ssl_bio_deleter_t {
    void operator() (BIO *b) {
        BIO_free(b);
    }
};

struct ssl_manapi_bio_deleter_t {
    void operator() (BIO_METHOD *b) {
        BIO_meth_free(b);
    }
};

struct manapi_bio_data_t {
    manapi::slice s;
};

int manapi_bio_write(BIO *bio, const char *buf, int size) {
    auto d = static_cast<manapi_bio_data_t *>(BIO_get_data(bio));
    if (!d || size < 0) return -1;
    auto err = d->s.push_back(buf, size);
    if (!err.ok()) {
        manapi_log_error("openssl: slice:push_back() failed: %s", err.msg().data());
        return -1;
    }
    BIO_set_retry_write(bio);
    return size;
}
int manapi_bio_read(BIO *bio, char *buf, int size) {
    auto d = static_cast<manapi_bio_data_t *>(BIO_get_data(bio));
    if (!d || size < 0) return -1;
    auto copy = std::min<std::size_t>(size, d->s.size());
    auto err =d->s.copy_to(buf, 0, copy);
    if (!err.ok()) {
        manapi_log_error("openssl: slice:copy_to() failed: %s", err.msg().data());
        return -1;
    }
    err = d->s.shift_add(copy);
    if (!err.ok()) {
        manapi_log_error("openssl: slice:shift_add() failed: %s", err.msg().data());
        return -1;
    }
    if (!d->s.empty())
        BIO_set_retry_read(bio);
    return copy;
}

long manapi_bio_ctrl(BIO *bio, int cmd, long arg1, void *arg2) {
    switch (cmd) {
        case BIO_CTRL_FLUSH:
            return 1;
        case BIO_CTRL_PUSH:
        case BIO_CTRL_POP:
            return 0;
        default:
            return 0;
    }
}
int manapi_bio_create(BIO *bio) {
    auto data = new manapi_bio_data_t ();
    if (!data) return 0;
    BIO_set_data(bio, data);
    BIO_set_init(bio, 1);
    return 1;
}
int manapi_bio_destroy(BIO *bio) {
    std::unique_ptr<manapi_bio_data_t> data;
    data.reset(static_cast<manapi_bio_data_t *>(BIO_get_data(bio)));
    if (!data)
        return 1;
    BIO_set_data(bio, nullptr);
    data->s.clear();
    return 1;
}
long manapi_bio_callback_ctrl(BIO *bio, int cmd, BIO_info_cb *fp) {
    return 0;
}

int manapi_bio_write_ex (BIO *bio, const char *buf, std::size_t size, std::size_t *res) {
    auto const rhs = manapi_bio_write(bio, buf, static_cast<int>(size));
    if (rhs < 0) return 0;
    if (res)
        *res = static_cast<std::size_t>(rhs);
    return 1;
}

int manapi_bio_read_ex (BIO *bio, char *buf, std::size_t size, std::size_t *res) {
    auto const rhs = manapi_bio_read(bio, buf, static_cast<int>(size));
    if (rhs < 0) return 0;
    if (res)
        *res = static_cast<std::size_t>(rhs);
    return 1;
}

int manapi_bio_puts (BIO *bio, const char *buf) {
    return manapi_bio_write(bio, buf, static_cast<int>(strlen(buf)));
}

int manapi_bio_gets (BIO *bio, char *buf, int size) {
    if (size <= 0) return -1;
    auto const rhs = manapi_bio_read(bio, buf, size - 1);
    if (rhs < 0)
        return rhs;
    buf[rhs] = '\0';
    return rhs;
}

int manapi_bio_recvmmsg(BIO *, BIO_MSG *, size_t, size_t, uint64_t, size_t *) {
    return 0;
}

int manapi_bio_sendmmsg(BIO *, BIO_MSG *, size_t, size_t, uint64_t, size_t *) {
    return 0;
}

BIO_METHOD *BIO_manapi_mem () noexcept {
    auto c = BIO_meth_new(BIO_TYPE_MEM, "ManapiOpenSslBio");
    if (!c) return nullptr;

    if (!BIO_meth_set_write(c, manapi_bio_write)
        ||!BIO_meth_set_read(c, manapi_bio_read)
        ||!BIO_meth_set_ctrl(c, manapi_bio_ctrl)
        ||!BIO_meth_set_create(c, manapi_bio_create)
        ||!BIO_meth_set_destroy(c, manapi_bio_destroy)
        ||!BIO_meth_set_callback_ctrl(c, manapi_bio_callback_ctrl)
        ||!BIO_meth_set_write_ex(c, manapi_bio_write_ex)
        ||!BIO_meth_set_read_ex(c, manapi_bio_read_ex)
        ||!BIO_meth_set_puts(c, manapi_bio_puts)
        ||!BIO_meth_set_gets(c, manapi_bio_gets)
        ||!BIO_meth_set_recvmmsg(c, manapi_bio_recvmmsg)
        ||!BIO_meth_set_sendmmsg(c, manapi_bio_sendmmsg)) {
        BIO_meth_free(c);
        return nullptr;
    }

    return c;
}

manapi::net::worker::OpenSSL_TLS::OpenSSL_TLS(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config) : TLS (std::move(site), std::move(wdata), config) {
    this->ssl_error_none_ = SSL_ERROR_NONE;
    this->ssl_error_syscall_ = SSL_ERROR_SYSCALL;
    this->ssl_error_want_read_ = SSL_ERROR_WANT_READ;
    this->ssl_error_want_write_ = SSL_ERROR_WANT_WRITE;
    this->ssl_error_zero_return_ = SSL_ERROR_ZERO_RETURN;
    this->ssl_error_ssl_ = SSL_ERROR_SSL;
    this->ssl_recv_shutdown_ = SSL_RECEIVED_SHUTDOWN;
    this->ssl_send_shutdown_ = SSL_SENT_SHUTDOWN;
    this->early_data_read_error_ = SSL_READ_EARLY_DATA_ERROR;
    this->early_data_read_finish_ = SSL_READ_EARLY_DATA_FINISH;
    this->early_data_read_success_ = SSL_READ_EARLY_DATA_SUCCESS;

    this->pool_data_ = nullptr;
}

manapi::net::worker::OpenSSL_TLS::~OpenSSL_TLS() {
    if (this->pool_data_) {
        std::lock_guard<std::mutex> lk (*this->pool_data_->mx);
        auto &wdata = this->pool_data_->data[this->deep_worker_id_];
        if (wdata.ref) {
            if (!(--wdata.ref)) {
                auto ctx_data = static_cast<ssl_worker_ctx_t *> (wdata.data);
                ctx_data->sessions_flush_timer.stop();
                delete ctx_data;
                wdata.data = nullptr;
            }
        }
    }

    SSL_CTX_free(static_cast<SSL_CTX*>(this->ctx));
}

std::shared_ptr<manapi::net::worker::OpenSSL_TLS> manapi::net::worker::OpenSSL_TLS::create(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::OpenSSL_TLS>(std::move(site), std::move(wdata), config.get());
    worker->self_ = worker;
    return std::move(worker);
}

void manapi::net::worker::OpenSSL_TLS::stop(std::function<void()> cb) {
    TLS::stop(std::move(cb));
}

void ssl_flush_sessions (std::mutex *mx, ssl_worker_ctx_t *ctx_data) {
    std::lock_guard<std::mutex> lk (*mx);
    auto &sessions = ctx_data->sessions;
    auto const current_time = ::time(nullptr);

    for (auto it = sessions.begin(); it != sessions.end(); ) {
        auto const created_at = SSL_SESSION_get_time_ex(it->second);
        auto const timeout_in = SSL_SESSION_get_timeout(it->second);

        if (created_at + timeout_in < current_time) {
            manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "openssl:Remove SSL session %p", it->second);
            SSL_SESSION_free(it->second);
            it = sessions.erase(it);
        }
        else {
            it++;
        }
    }
}

manapi::future<manapi::error::status> manapi::net::worker::OpenSSL_TLS::init(std::size_t deep) {
    auto res = co_await TLS::init(deep + 1);
    if (!res.ok())
        co_return std::move(res);

    this->deep_worker_id_ = deep;

    try {
        this->pool_data_ = &this->worker_data_->as<http::server_ctx::worker_data_t>()->pools[this->worker_pool_id_];
        std::lock_guard<std::mutex> lk (*this->pool_data_->mx);

        if (this->pool_data_->data.size() <= deep)
            this->pool_data_->data.resize(deep + 1);

        if (!(this->pool_data_->data[deep].ref))
            this->pool_data_->data[deep].data = new ssl_worker_ctx_t{};

        auto ctx_data = static_cast<ssl_worker_ctx_t *>(this->pool_data_->data[deep].data);
        this->pool_data_->data[deep].ref++;

        if (!ctx_data->sessions_flush_timer) {
            ctx_data->sessions_flush_timer = manapi::async::current()->timerpool()->append_interval_sync(
                10000, [ctx_data, mx = this->pool_data_->mx.get()](const manapi::timer& t)
                    ->void {
                ssl_flush_sessions (mx, ctx_data);
            }).unwrap();
        }

        if (!ctx_data->ctx) {
            auto strtls = this->config_->get_config_param<std::string> (this->config_->ssl, "tls", "1.3");
            int tls_version = http::versions::TLS_v1_3;
            if (strtls == "1.3") tls_version = http::versions::TLS_v1_3;
            else if (strtls == "1.2") tls_version = http::versions::TLS_v1_2;
            else if (strtls == "1.1") tls_version = http::versions::TLS_v1_1;
            auto status = this->ssl_create_context(tls_version);
            if (!status.ok())
                co_return status.err();
            ctx_data->ctx = static_cast<SSL_CTX*>(status.unwrap());
            res = this->ssl_configure_context(ctx_data->ctx);
            if (!res.ok())
                co_return std::move(res);
        }
        this->ctx = ctx_data->ctx;
        co_return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "openssl_tls:Failed", e.what());
    }
    co_return error::status_internal("openssl_tls:Failed");
}

manapi::net::http::server_ctx::pool_t * manapi::net::worker::OpenSSL_TLS::openssl_pool_data_() MANAPIHTTP_NOEXPECT {
    return this->pool_data_;
}

bool manapi::net::worker::OpenSSL_TLS::ssl_is_init_fininshed_(void *ssl) MANAPIHTTP_NOEXPECT {
    ERR_clear_error();
    return SSL_is_init_finished(static_cast<SSL*>(ssl));
}

int manapi::net::worker::OpenSSL_TLS::ssl_get_error_(void *ssl, int rhs) MANAPIHTTP_NOEXPECT {
    return SSL_get_error(static_cast<SSL*>(ssl), rhs);
}

int manapi::net::worker::OpenSSL_TLS::ssl_accept_(void *ssl) MANAPIHTTP_NOEXPECT {
    ERR_clear_error();
    return SSL_accept(static_cast<SSL*>(ssl));
}

void * manapi::net::worker::OpenSSL_TLS::ssl_new_(void *ctx) MANAPIHTTP_NOEXPECT {
    ERR_clear_error();
    return SSL_new(static_cast<SSL_CTX*>(ctx));
}

int manapi::net::worker::OpenSSL_TLS::ssl_write_(void *ssl, const void *buff, int size) MANAPIHTTP_NOEXPECT {
    ERR_clear_error();
    return SSL_write(static_cast<SSL*>(ssl), buff, size);
}

int manapi::net::worker::OpenSSL_TLS::ssl_read_(void *ssl, void *buff, int size) MANAPIHTTP_NOEXPECT {
    ERR_clear_error();
    return SSL_read(static_cast<SSL*>(ssl), buff, size);
}

int manapi::net::worker::OpenSSL_TLS::ssl_shutdown_(void *ssl) MANAPIHTTP_NOEXPECT {
    ERR_clear_error();
    return SSL_shutdown(static_cast<SSL*>(ssl));
}

void manapi::net::worker::OpenSSL_TLS::ssl_set_shutdown_(void *ssl, int flags) MANAPIHTTP_NOEXPECT {
    SSL_set_shutdown(static_cast<SSL*>(ssl), flags);
}

void manapi::net::worker::OpenSSL_TLS::ssl_free_(void *ssl) MANAPIHTTP_NOEXPECT {
    SSL_free(static_cast<SSL*>(ssl));
}

int manapi::net::worker::OpenSSL_TLS::ssl_bio_read_(void *wbio, void *buff, int size) MANAPIHTTP_NOEXPECT {
    return BIO_read(static_cast<BIO*>(wbio), buff, static_cast<int>(size));
}

int manapi::net::worker::OpenSSL_TLS::ssl_bio_write_(void *rbio, const void *buff, int size) MANAPIHTTP_NOEXPECT {
    return BIO_write(static_cast<BIO*>(rbio), buff, static_cast<int>(size));
}

int manapi::net::worker::OpenSSL_TLS::ssl_bio_should_retry_(void *bio) MANAPIHTTP_NOEXPECT {
    return BIO_should_retry(static_cast<BIO*>(bio));
}

int manapi::net::worker::OpenSSL_TLS::ssl_read_early_data_(void *ssl, void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXPECT {
    ERR_clear_error();
    return SSL_read_early_data(static_cast<SSL*>(ssl), buf, num, readbytes);
}

int manapi::net::worker::OpenSSL_TLS::ssl_write_early_data_(void *ssl, const void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXPECT {
    ERR_clear_error();
    return SSL_write_early_data(static_cast<SSL*>(ssl), buf, num, readbytes);
}

bool manapi::net::worker::OpenSSL_TLS::ssl_early_data_is_enabled_(void *ctx) MANAPIHTTP_NOEXPECT {
    return SSL_CTX_get_max_early_data(static_cast<SSL_CTX*>(ctx)) > 0;
}

// SSL_SESSION *ssl_get_session(SSL *ssl, const unsigned char *data, int len, int *copy) MANAPIHTTP_NOEXPECT {
//     auto const ctx = SSL_get_SSL_CTX(ssl);
//     auto const w = static_cast<manapi::net::worker::OpenSSL_TLS*>(SSL_CTX_get_app_data(ctx));
//     if (!w)
//         return nullptr;
//
//     std::string_view id (reinterpret_cast<const char*>(data), len);
//     auto pool_data = w->openssl_pool_data_();
//     if (!pool_data)
//         return nullptr;
//
//     auto const deep = w->deep_worker_id();
//     if (pool_data->data.size() <= deep)
//         return nullptr;
//
//     auto ctx_data = static_cast<ssl_worker_ctx_t *> (pool_data->data[deep].data);
//     if (!ctx_data)
//         return nullptr;
//
//     std::lock_guard<std::mutex> lk (*pool_data->mx);
//     auto it = ctx_data->sessions.find(id);
//
//     if (it == ctx_data->sessions.end())
//         return nullptr;
//
//     auto const res = it->second;
//
//     *copy = 1;
//     manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "openssl:Get SSL session %p", res);
//
//     return res;
// }
//
// int ssl_new_session(SSL *ssl, SSL_SESSION *sess) MANAPIHTTP_NOEXPECT {
//     try {
//         auto const ctx = SSL_get_SSL_CTX(ssl);
//         auto const w = static_cast<manapi::net::worker::OpenSSL_TLS*>(SSL_CTX_get_app_data(ctx));
//         if (!w || !sess)
//             return 0;
//
//         // sess = SSL_SESSION_dup(sess);
//         // if (!sess)
//         //     return 0;
//
//         uint32_t id_len;
//         auto id_src = SSL_SESSION_get_id(sess, &id_len);
//
//         std::string_view id (reinterpret_cast<const char*>(id_src), id_len);
//
//
//         auto pool_data = w->openssl_pool_data_();
//         if (!pool_data)
//             return 0;
//
//         auto const deep = w->deep_worker_id();
//         if (deep >= pool_data->data.size())
//             return 0;
//
//         auto ctx_data = static_cast<ssl_worker_ctx_t *> (pool_data->data[deep].data);
//         if (!ctx_data)
//             return 0;
//
//         std::lock_guard<std::mutex> lk (*pool_data->mx);
//
//         if (!SSL_SESSION_up_ref(sess))
//             return 0;
//
//         if (!ctx_data->sessions.insert({std::string{id}, sess}).second) {
//             SSL_SESSION_free(sess);
//             return 0;
//         }
//
//         manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "openssl:New SSL session %p", sess);
//
//         return 1;
//     }
//     catch (std::exception const &e) {
//         manapi_log_error("%s failed due to %s", "OpenSSL:new session", e.what());
//     }
//     return 0;
// }
//
// void ssl_remove_session (SSL_CTX *ctx, SSL_SESSION *sess) MANAPIHTTP_NOEXPECT {
//     std::size_t deep;
//     manapi::net::http::server_ctx::pool_t *pool_data;
//     std::string_view id;
//     ssl_worker_ctx_t *ctx_data;
//     const unsigned char*id_src;
//     uint32_t id_len;
//
//     auto const w = static_cast<manapi::net::worker::OpenSSL_TLS*>(SSL_CTX_get_app_data(ctx));
//     if (!w || !sess)
//         goto finish;
//
//     id_src = SSL_SESSION_get_id(sess, &id_len);
//
//     id = std::string_view(reinterpret_cast<const char*>(id_src), id_len);
//
//     pool_data = w->openssl_pool_data_();
//     if (!pool_data)
//         goto finish;
//
//     deep = w->deep_worker_id();
//     if (deep >= pool_data->data.size())
//         goto finish;
//
//     ctx_data = static_cast<ssl_worker_ctx_t *> (pool_data->data[deep].data);
//     if (ctx_data) {
//         std::lock_guard<std::mutex> lk (*pool_data->mx);
//         auto it = ctx_data->sessions.find(id);
//         if (it != ctx_data->sessions.end()) {
//             manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "openssl:Remove SSL session %p", sess);
//             SSL_SESSION_free(it->second);
//             ctx_data->sessions.erase(it);
//         }
//     }
// finish:
// }

int manapi::net::worker::OpenSSL_TLS::recv_setup_connection(const shared_conn &conn, tls_connection_t *data) {
    ERR_clear_error();

    data->wbio = BIO_new(BIO_s_mem());
    if (!data->wbio)
        goto err;

    data->rbio = BIO_new(BIO_s_mem());
    if (!data->rbio)
        goto err;

    SSL_set_accept_state(static_cast<SSL*>(data->ssl));

    SSL_set_bio(static_cast<SSL*>(data->ssl), static_cast<BIO*>(data->rbio), static_cast<BIO*>(data->wbio));

    SSL_set_app_data (static_cast<SSL*>(data->ssl), conn.get());

    return ERR_OK;
err:
    return ERR_INTERNAL;
}


manapi::error::status_or<void *> manapi::net::worker::OpenSSL_TLS::ssl_create_context(size_t version) {
    if (this->ctx)
        return this->ctx;

    auto const cipher_list = this->config_->get_config_param<std::string>(this->config_->ssl, "ciphers", {});
    auto const ssl_v2 = this->config_->get_config_param<bool>(this->config_->ssl, "ssl_v2", false);
    auto const ssl_v3 = this->config_->get_config_param<bool>(this->config_->ssl, "ssl_v3", false);
    auto const ticket = this->config_->get_config_param<bool>(this->config_->ssl, "ticket", true);
    auto const sess_timeout = this->config_->get_config_param<uint32_t>(this->config_->ssl, "sess_timeout", 300);
    auto const sess_cache = this->config_->get_config_param<bool>(this->config_->ssl, "sess_cache", false);
    auto const sess_cache_size = this->config_->get_config_param<uint32_t>(this->config_->ssl, "sess_cache_size", 1024 * 20);
    auto const max_early_data = this->config_->get_config_param<std::size_t>(this->config_->ssl, "max_early_data", 0);

    this->ssl_session_ctx_id = 1;

    const SSL_METHOD *method;
    SSL_CTX *ctx;

    switch (version)
    {
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        case http::versions::TLS_v1:      method = TLSv1_server_method();     break;
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        case http::versions::TLS_v1_1:    method = TLSv1_1_server_method();   break;
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        case http::versions::TLS_v1_2:    method = TLSv1_2_server_method();   break;
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        case http::versions::TLS_v1_3:    method = TLS_server_method();       break;
        default: THROW_MANAPIHTTP_EXCEPTION(ERR_FAILED_PRECONDITION,
            "can not find the initialization method openssl (tls_version): {}", version);
    }



    ERR_clear_error();
    ctx = SSL_CTX_new(method);

    if (!ctx)
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL, "{}", "cannot create the openssl context for the tcp connection");

    SSL_CTX_set_app_data(ctx, this);

    auto ktls = http::config::get_config_param<bool>(
        this->config_->ssl, "ktls", true);
    auto single_dh_use = http::config::get_config_param<bool>(
        this->config_->ssl, "single_dh_use", false);
    auto ktls_tx_zerocopy_senfile = http::config::get_config_param<bool>(
        this->config_->ssl, "ktls_tx_zerocopy_senfile", true);

    if (ktls)
        SSL_CTX_set_options(ctx, SSL_OP_ENABLE_KTLS);
    if (ktls_tx_zerocopy_senfile)
        SSL_CTX_set_options(ctx, SSL_OP_ENABLE_KTLS_TX_ZEROCOPY_SENDFILE);

    if (max_early_data) {
        if (!SSL_CTX_set_max_early_data(ctx, max_early_data)) {
            manapi_log_error("openssl: %s failed", "SSL_CTX_set_max_early_data");
        }
    }
    SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);
    // SSL_CTX_set_min_proto_version(ctx, 0);
    // SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);

    // SSL_CTX_set_max_send_fragment(ctx, this->config->buffer_size());
    // SSL_CTX_set_default_read_buffer_len(ctx, this->config->buffer_size());
    if (!ssl_v2)
        SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);
    if (!ssl_v3)
        SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv3);
    if (!ticket)
        SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET);

    if (sess_cache) {
        //SSL_SESS_CACHE_NO_INTERNAL_STORE
        SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);

        if (!SSL_CTX_set_session_id_context(ctx,
            reinterpret_cast<const unsigned char *>(&this->ssl_session_ctx_id), sizeof(this->ssl_session_ctx_id)))
            goto err;

        // SSL_CTX_sess_set_get_cb(ctx, ssl_get_session);
        // SSL_CTX_sess_set_new_cb(ctx, ssl_new_session);
        // SSL_CTX_sess_set_remove_cb(ctx, ssl_remove_session);
    }
    else {
        SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
    }

    SSL_CTX_set_read_ahead(ctx, 1);

    //long cache_mode = SSL_SESS_CACHE_SERVER;
    SSL_CTX_sess_set_cache_size(ctx, sess_cache_size);

    if (!SSL_CTX_set_timeout(ctx, sess_timeout))
        goto err;

    if (single_dh_use)
        SSL_CTX_set_options(ctx, SSL_OP_SINGLE_DH_USE);

    SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS
        |SSL_MODE_AUTO_RETRY
        |SSL_MODE_ENABLE_PARTIAL_WRITE
        |SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER
    );

    if (cipher_list.empty()) {
        SSL_CTX_set_options(ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
    }
    else {
        // TLSv1.2>= SSL_CTX_set_cipher_list
        // TLSv1.3<= SSL_CTX_set_ciphersuites
        if (!SSL_CTX_set_ciphersuites(ctx, static_cast<const char *>(cipher_list.data())))
            goto err;

    }

    SSL_CTX_set_alpn_select_cb(ctx, [] (SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
        unsigned int inlen, void *arg) -> int {
        auto const worker = static_cast<OpenSSL_TLS *> (arg);

        int j = 0;
        for (int i = 0; i < inlen;j++) {
            int plen = in[i++];
            std::string_view buff (reinterpret_cast<const char *>(in) + i, reinterpret_cast<const char *>(in) + i + plen);
            if ((buff == "h3" && worker->config_->contains_http_version(http::versions::HTTP_v3))
                || (buff == "h2" && worker->config_->contains_http_version(http::versions::HTTP_v2))
                || (buff == "http/1.1" && worker->config_->contains_http_version(http::versions::HTTP_v1_1))
                || (buff == "http/1.0" && worker->config_->contains_http_version(http::versions::HTTP_v1_0))
                || (buff == "http/0.9" && worker->config_->contains_http_version(http::versions::HTTP_v0_9)))
                goto choise;

            i += plen;
            continue;
            choise: {
                if (worker->global_.alpn_cb) {
                    auto rhs = worker->global_.alpn_cb(&worker->global_, buff.data(), buff.size(), worker);
                    if (rhs < 0)
                        return SSL_TLSEXT_ERR_ALERT_FATAL;

                    auto conn = static_cast<worker::connection *>(SSL_get_app_data(ssl));
                    conn->version = rhs;
                }

                *out = reinterpret_cast<const unsigned char *> (buff.data());
                *outlen = buff.size();
                return SSL_TLSEXT_ERR_OK;
            }
        }

        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }, this);

    return ctx;
err:
    std::unique_ptr<BIO, ssl_bio_deleter_t> bio;
    bio.reset(BIO_new(BIO_s_mem()));
    ERR_print_errors(bio.get());
    char *buf;
    size_t len = BIO_get_mem_data(bio.get(), &buf);

    manapi_log_error("%s due to %.*s", "openssl_tls:create context failed", len, buf);
    return error::status_internal("openssl_tls:create context failed");
}

manapi::error::status manapi::net::worker::OpenSSL_TLS::ssl_configure_context(void*ctx) {
    ERR_clear_error();

    auto verify_peer = this->config_->get_config_param<bool>(this->config_->ssl, "verify_peer", true);
    auto cert = this->config_->get_config_param<std::string>(this->config_->ssl, "cert", {});
    auto key = this->config_->get_config_param<std::string>(this->config_->ssl, "key", {});

    if (SSL_CTX_use_certificate_file(static_cast<SSL_CTX*>(ctx), cert.data(), SSL_FILETYPE_PEM) <= 0)
        return error::status_failed_precondition("openssl_tls:Cannot use cert file");


    if (SSL_CTX_use_PrivateKey_file(static_cast<SSL_CTX*>(ctx), key.data(), SSL_FILETYPE_PEM) <= 0)
        return error::status_failed_precondition("openssl_tls:Cannot use private key file");


    if (!SSL_CTX_check_private_key(static_cast<SSL_CTX*>(ctx)))
        return error::status_failed_precondition("openssl_tls:Private key does not match the certificate public key");

    SSL_CTX_set_verify(static_cast<SSL_CTX*>(ctx), verify_peer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_verify_depth(static_cast<SSL_CTX*>(ctx), 1);
    return error::status_ok();
}



#endif // MANAPIHTTP_OPENSSL_DEPENDENCY