#include "worker/ManapiWolfSslOverTcp.hpp"
#include "../include/ManapiUtils.hpp"

#if MANAPIHTTP_WOLFSSL_DEPENDENCY

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfio.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfio.h>
#include <wolfssl/openssl/compat_types.h>

#ifndef OPENSSL_EXTRA
static_assert(false, "WolfSSL must be built with --enable-all and --enable-opensslextra options");
#endif

#include <iostream>
#include <csignal>
#include <utility>
#include <vector>
#include <memory.h>
#include <arpa/inet.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <netdb.h>
#include <set>

#include "ManapiString.hpp"
#include "ManapiUtils.hpp"
#include "ManapiInitTools.hpp"
#include "std/ManapiAsyncSocket.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/worker/ManapiBaseUtils.hpp"

enum wssl_ex_data_codes {
    WSSL_EX_DATA_WORKER_CTX = 0
};

class wssl_worker_ctx_t {
public:
    wssl_worker_ctx_t() : ctx(nullptr) {}
    std::map<std::string, WOLFSSL_SESSION*, std::less<>> sessions;
    WOLFSSL_CTX *ctx;
    manapi::timer sessions_flush_timer;
    std::string alpn{};
};

struct wssl_bio_deleter_t {
    void operator() (WOLFSSL_BIO *b) {
        wolfSSL_BIO_free(b);
    }
};

static std::string wgenerate_alpn_ossltest (const std::vector<std::string_view> &tests) {
    std::string b;
    std::size_t s = 0;

    for (auto &test : tests) {
        s += 1 + test.size();
    }

    b.reserve(s);

    auto it = tests.begin();
    goto cont;
    for (; it != tests.end(); it++) {
        b.push_back(',');
        cont:
        b.append(it->data(), it->size());
    }

    return std::move(b);
}

manapi::net::worker::WolfSSL_TLS::WolfSSL_TLS(std::shared_ptr<net::worker::site> site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config) : TLS (std::move(site), std::move(wdata), config) {
    this->ssl_error_none_ = WOLFSSL_ERROR_NONE;
    this->ssl_error_syscall_ = WOLFSSL_ERROR_SYSCALL;
    this->ssl_error_want_read_ = WOLFSSL_ERROR_WANT_READ;
    this->ssl_error_want_write_ = WOLFSSL_ERROR_WANT_WRITE;
    this->ssl_error_zero_return_ = WOLFSSL_ERROR_ZERO_RETURN;
    this->ssl_error_ssl_ = WOLFSSL_ERROR_SSL;
    this->ssl_recv_shutdown_ = WOLFSSL_RECEIVED_SHUTDOWN;
    this->ssl_send_shutdown_ = WOLFSSL_SENT_SHUTDOWN;
    this->ssl_shutdown_sucess = WOLFSSL_SUCCESS;
    this->ssl_shutdown_fatal_error = WOLFSSL_FATAL_ERROR;
    this->ssl_shutdown_not_done = WOLFSSL_SHUTDOWN_NOT_DONE;
    this->pool_data_ = nullptr;
#if 0
    this->early_data_read_error_ = WOLFSSL_READ_EARLY_DATA_ERROR;
    this->early_data_read_finish_ = WOLFSSL_READ_EARLY_DATA_FINISH;
    this->early_data_read_success_ = WOLFSSL_READ_EARLY_DATA_SUCCESS;
#else
    this->early_data_read_error_ = 0;
    this->early_data_read_finish_ = 1;
    this->early_data_read_success_ = 2;
#endif
}

manapi::net::worker::WolfSSL_TLS::~WolfSSL_TLS() {
    if (this->pool_data_) {
        std::lock_guard<std::mutex> lk (*this->pool_data_->mx);
        auto &wdata = this->pool_data_->data[this->deep_worker_id_];
        if (wdata.ref) {
            if (!(--wdata.ref)) {
                auto ctx_data = static_cast<wssl_worker_ctx_t *> (wdata.data);
                ctx_data->sessions_flush_timer.stop();
                wolfSSL_CTX_free(ctx_data->ctx);
                delete ctx_data;
                wdata.data = nullptr;
            }
        }
    }
}

manapi::future<manapi::status> manapi::net::worker::WolfSSL_TLS::init(std::size_t deep) {
    auto res = co_await TLS::init(deep + 1);
    if (!res)
        co_return std::move(res);

    this->deep_worker_id_ = deep;

    try {
        this->pool_data_ = &this->worker_data_->as<http::server_ctx::worker_data_t>()->pools[this->worker_pool_id_];
        std::lock_guard<std::mutex> lk (*this->pool_data_->mx);

        if (this->pool_data_->data.size() <= deep)
            this->pool_data_->data.resize(deep + 1);

        if (!(this->pool_data_->data[deep].ref))
            this->pool_data_->data[deep].data = new wssl_worker_ctx_t ();

        auto ctx_data = static_cast<wssl_worker_ctx_t *>(this->pool_data_->data[deep].data);
        this->pool_data_->data[deep].ref++;

        if (!ctx_data->sessions_flush_timer) {
            // ctx_data->sessions_flush_timer = manapi::async::current()->timerpool()->append_interval_sync(
            //     10000, [ctx_data, mx = this->pool_data_->mx.get()](const manapi::timer& t)
            //         ->void {
            //     ssl_flush_sessions (mx, ctx_data);
            // }).unwrap();
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
            ctx_data->ctx = static_cast<WOLFSSL_CTX*>(status.unwrap());
            res = this->ssl_configure_context(ctx_data->ctx, this->pool_data_, deep);

            if (!res.ok())
                co_return std::move(res);
        }
        this->ctx = ctx_data->ctx;

        co_return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "openssl_tls:Failed", e.what());
    }
    co_return status_internal("openssl_tls:Failed");
}

std::shared_ptr<manapi::net::worker::WolfSSL_TLS> manapi::net::worker::WolfSSL_TLS::create(std::shared_ptr<net::worker::site> site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config* config) {
    auto worker = std::make_shared<worker::WolfSSL_TLS>(std::move(site), std::move(wdata), config);
    return std::move(worker);
}

void manapi::net::worker::WolfSSL_TLS::stop(std::function<void()> cb) {
    TLS::stop(std::move(cb));
}

bool manapi::net::worker::WolfSSL_TLS::ssl_is_init_fininshed_(void *ssl) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_is_init_finished(static_cast<WOLFSSL *>(ssl));
}

int manapi::net::worker::WolfSSL_TLS::ssl_get_error_(void *ssl, int rhs) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_get_error(static_cast<WOLFSSL *>(ssl), rhs);
}

int manapi::net::worker::WolfSSL_TLS::ssl_accept_(void *ssl) MANAPIHTTP_NOEXCEPT {
    auto const rhs = wolfSSL_accept(static_cast<WOLFSSL *>(ssl));

#if MANAPIHTTP_WOLFSSL_WITH_ALPN
    if (rhs == 1) {
        char *alpn;
        uint16_t alpn_size;
        wolfSSL_ALPN_GetProtocol(static_cast<WOLFSSL*>(ssl), &alpn, &alpn_size);
        auto conn = static_cast<worker::connection *>(wolfSSL_get_app_data(static_cast<WOLFSSL*>(ssl)));
        if (conn) {
            if (alpn && alpn_size) {
                auto alpn_rhs = this->global_.alpn_cb(&this->global_, alpn, alpn_size, this);

                if (alpn_rhs < 0) {
                    conn->version = 1;
                }
                else {
                    conn->version = rhs;
                }

            }
            else {
                conn->version = 1;
            }
        }
    }
#endif

    return rhs;
}

void * manapi::net::worker::WolfSSL_TLS::ssl_new_(void *ctx) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_new(static_cast<WOLFSSL_CTX *>(ctx));
}

int manapi::net::worker::WolfSSL_TLS::ssl_write_(void *ssl, const void *buff, int size) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_write(static_cast<WOLFSSL *>(ssl), buff, size);
}

int manapi::net::worker::WolfSSL_TLS::ssl_read_(void *ssl, void *buff, int size) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_read(static_cast<WOLFSSL *>(ssl), buff, size);
}

int manapi::net::worker::WolfSSL_TLS::ssl_shutdown_(void *ssl) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_shutdown(static_cast<WOLFSSL *>(ssl));
}

void manapi::net::worker::WolfSSL_TLS::ssl_set_shutdown_(void *ssl, int flags) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_set_shutdown(static_cast<WOLFSSL *>(ssl), flags);
}

void manapi::net::worker::WolfSSL_TLS::ssl_free_(void *ssl) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_free(static_cast<WOLFSSL *>(ssl));
}

int manapi::net::worker::WolfSSL_TLS::ssl_bio_read_(void *wbio, void *buff, int size) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_BIO_read(static_cast<WOLFSSL_BIO*>(wbio), buff, static_cast<int>(size));
}

int manapi::net::worker::WolfSSL_TLS::ssl_bio_should_retry_(void *bio) MANAPIHTTP_NOEXCEPT {
    return true;
}

int manapi::net::worker::WolfSSL_TLS::ssl_bio_write_(void *rbio, const void *buff, int size) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_BIO_write(static_cast<WOLFSSL_BIO*>(rbio), buff, static_cast<int>(size));
}

int manapi::net::worker::WolfSSL_TLS::ssl_read_early_data_(void *ssl, void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXCEPT {
#if 0
    return wolfSSL_read_early_data(static_cast<WOLFSSL*>(ssl), buf, num, readbytes);
#else
    return this->early_data_read_finish_;
#endif
}

int manapi::net::worker::WolfSSL_TLS::ssl_write_early_data_(void *ssl, const void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXCEPT {
#if 0
    return wolfSSL_write_early_data(static_cast<WOLFSSL*>(ssl), buf, num, readbytes);
#else
    return 0;
#endif
}

bool manapi::net::worker::WolfSSL_TLS::ssl_early_data_is_enabled_(void *ctx) MANAPIHTTP_NOEXCEPT {
#if 0
    return true;
#else
    return false;
#endif

}

int manapi::net::worker::WolfSSL_TLS::recv_setup_connection(const shared_conn &conn, tls_connection_t *data) MANAPIHTTP_NOEXCEPT {
    ERR_clear_error();
    WOLFSSL_CTX *ctx;
    wssl_worker_ctx_t *ctx_data;


    data->wbio = BIO_new(BIO_s_mem());
    if (!data->wbio) {
        goto err;
    }

    data->rbio = BIO_new(BIO_s_mem());
    if (!data->rbio) {
        goto err;
    }

    SSL_set_accept_state(static_cast<WOLFSSL*>(data->ssl));

    SSL_set_bio(static_cast<WOLFSSL*>(data->ssl), static_cast<WOLFSSL_BIO*>(data->rbio), static_cast<WOLFSSL_BIO*>(data->wbio));

    if (!SSL_set_app_data(static_cast<WOLFSSL*>(data->ssl), data))
        goto err;

    ctx = wolfSSL_get_SSL_CTX(static_cast<WOLFSSL*>(data->ssl));
    ctx_data = static_cast<wssl_worker_ctx_t*>(wolfSSL_CTX_get_ex_data(ctx, WSSL_EX_DATA_WORKER_CTX));

    if (!ctx_data)
        goto err;
#if MANAPIHTTP_WOLFSSL_WITH_ALPN
    if (WOLFSSL_SUCCESS != wolfSSL_UseALPN(static_cast<WOLFSSL *>(data->ssl),
        ctx_data->alpn.data(),
        ctx_data->alpn.size(),
        WOLFSSL_ALPN_FAILED_ON_MISMATCH))
        goto err;
#endif

    return 0;
    err:
        return ERR_INTERNAL;
}


manapi::status_or<void *> manapi::net::worker::WolfSSL_TLS::ssl_create_context(size_t version) MANAPIHTTP_NOEXCEPT {
    if (this->ctx)
        return this->ctx;

    manapi::status status;
    try {
        using ci = manapi::internal::config_interface;
        std::string_view ret;
        auto const cipher_list = ci::get_config_param<std::string>(this->config_->ssl, "ciphers", {});
        auto const ssl_v2 = ci::get_config_param<bool>(this->config_->ssl, "ssl_v2", false);
        auto const ssl_v3 = ci::get_config_param<bool>(this->config_->ssl, "ssl_v3", false);
        auto const ticket = ci::get_config_param<bool>(this->config_->ssl, "ticket", true);
        auto const sess_timeout = ci::get_config_param<uint32_t>(this->config_->ssl, "sess_timeout", 300);
        auto const sess_cache = ci::get_config_param<bool>(this->config_->ssl, "sess_cache", false);
        auto const sess_cache_size = ci::get_config_param<uint32_t>(this->config_->ssl, "sess_cache_size", 1024 * 20);
        auto const max_early_data = ci::get_config_param<std::size_t>(this->config_->ssl, "max_early_data", 0);
        WOLFSSL_METHOD *method;
        WOLFSSL_CTX *ctx;

        switch (version)
        {
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            // ReSharper disable once CppDeprecatedEntity
            case http::versions::TLS_v1_2:
                method = wolfTLSv1_2_server_method();
            break;
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            case http::versions::TLS_v1_3:
                method = wolfTLS_server_method();
            break;
            default:
                return status_internal("wolfssl:can not find the initialization method openssl (tls_version)");
        }


        ctx = wolfSSL_CTX_new(method);

        if (!ctx) {
            return status_resource_exhausted();
        }

        if (sess_cache) {
            wolfSSL_CTX_set_session_cache_mode(ctx, WOLFSSL_SESS_CACHE_SERVER);
#if MANAPIHTTP_WOLFSSL_WITH_ALPN
            if (!wolfSSL_CTX_set_session_id_context(ctx,
                reinterpret_cast<const unsigned char *>(&this->ssl_session_ctx_id), sizeof(this->ssl_session_ctx_id)))
                goto err;
#endif
        }
        else {
            wolfSSL_CTX_set_session_cache_mode(ctx, WOLFSSL_SESS_CACHE_OFF);
        }

        int options = 0;

        if (!ssl_v2)
            options |= WOLFSSL_OP_NO_SSLv2;

        if (!ssl_v3)
            options |= WOLFSSL_OP_NO_SSLv3;

        if (!ticket)
            options |= WOLFSSL_OP_NO_TICKET;

        options |= WOLFSSL_OP_NO_COMPRESSION;

        wolfSSL_CTX_sess_set_cache_size(ctx, sess_cache_size);

        if (!wolfSSL_CTX_set_timeout(ctx, sess_timeout))
            goto err;

        wolfSSL_CTX_set_options(ctx, options);

        wolfSSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS
            |SSL_MODE_AUTO_RETRY
            |SSL_MODE_ENABLE_PARTIAL_WRITE
            |WOLFSSL_MODE_ACCEPT_MOVING_WRITE_BUFFER
        );

        if (cipher_list.empty()) {
            wolfSSL_CTX_set_options(ctx, WOLFSSL_OP_CIPHER_SERVER_PREFERENCE);
        }
        else {
            // TLSv1.2>= SSL_CTX_set_cipher_list
            // TLSv1.3<= SSL_CTX_set_ciphersuites
            if (!wolfSSL_CTX_set_cipher_list(ctx, static_cast<const char *>(cipher_list.data())))
                goto err;

        }

        return ctx;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s:%s due to %s", "wolfssl", "ssl_create_context:Failed", e.what());
        status = manapi::status_internal("ssl_create_context:Failed");
    }
err:
    return std::move(status);
}

manapi::status manapi::net::worker::WolfSSL_TLS::ssl_configure_context(void *ctx, http::server_ctx::pool_t *pool_data, std::size_t deeplvl) MANAPIHTTP_NOEXCEPT {
    try {
        using ci = manapi::internal::config_interface;

        auto verify_peer = ci::get_config_param<bool>(this->config_->ssl, "verify_peer", true);
        auto cert = ci::get_config_param<std::string>(this->config_->ssl, "cert", {});
        auto key = ci::get_config_param<std::string>(this->config_->ssl, "key", {});

        if (wolfSSL_CTX_use_certificate_file(static_cast<WOLFSSL_CTX *>(ctx), cert.data(), SSL_FILETYPE_PEM) <= 0) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %.*s", "wolfssl:couldn't load certificate file",
                cert.size(), cert.data());
            return manapi::status_internal("wolfssl:couldn't load certificate file");
        }

        if (wolfSSL_CTX_use_PrivateKey_file(static_cast<WOLFSSL_CTX *>(ctx), key.data(), SSL_FILETYPE_PEM) <= 0) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s: %.*s", "wolfssl:couldn't load key file",
                key.size(), key.data());
            return manapi::status_internal("wolfssl:couldn't load key file");
        }

        if (!wolfSSL_CTX_check_private_key(static_cast<WOLFSSL_CTX *>(ctx))) {
            return manapi::status_failed_precondition("wolfssl:Private key does not match the certificate public key");
        }

        wolfSSL_CTX_set_verify(static_cast<WOLFSSL_CTX *>(ctx), verify_peer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
        wolfSSL_CTX_set_verify_depth(static_cast<WOLFSSL_CTX *>(ctx), 1);

        auto ctx_data = static_cast<wssl_worker_ctx_t *>(this->pool_data_->data[deeplvl].data);\

        auto alpns = ci::get_config_param<std::string>(this->config_->ssl, "alpns", {});

        {
            /* alpns */
            auto list = manapi::string::split(alpns, ',');
            auto config_list = this->config()->alpns();

            list.insert(list.end(), config_list.begin(), config_list.end());\
            ctx_data->alpn = (wgenerate_alpn_ossltest(list));
        }

        if (!SSL_CTX_set_ex_data(static_cast<WOLFSSL_CTX *>(ctx), WSSL_EX_DATA_WORKER_CTX, ctx_data)) {
            return status_resource_exhausted();
        }

        return manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "wolfssl:configure", e.what());
        return manapi::status_internal("wolfssl:configure");
    }
}

#endif // MANAPIHTTP_WOLFSSL_DEPENDENCY