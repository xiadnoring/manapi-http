#include "worker/ManapiWolfSslOverTcp.hpp"
#include "../include/ManapiUtils.hpp"
#include "ManapiString.hpp"
#include "../include/ManapiUtils.hpp"

#if MANAPIHTTP_WOLFSSL_DEPENDENCY

#include <wolfssl/options.h>
#include <wolfssl/wolfio.h>
#include <wolfssl/ssl.h>

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

#include "ManapiUtils.hpp"
#include "ManapiInitTools.hpp"
#include "async/ManapiAsyncSocket.hpp"

struct ssl_bio_deleter_t {
    void operator() (BIO *b) {
        wolfSSL_BIO_free(b);
    }
};

manapi::net::worker::WolfSSL_TLS::WolfSSL_TLS(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config) : TLS (std::move(site), std::move(wdata), config) {
    this->ssl_error_none_ = WOLFSSL_ERROR_NONE;
    this->ssl_error_syscall_ = WOLFSSL_ERROR_SYSCALL;
    this->ssl_error_want_read_ = WOLFSSL_ERROR_WANT_READ;
    this->ssl_error_want_write_ = WOLFSSL_ERROR_WANT_WRITE;
    this->ssl_error_zero_return_ = WOLFSSL_ERROR_ZERO_RETURN;
    this->ssl_error_ssl_ = WOLFSSL_ERROR_SSL;
    this->ssl_recv_shutdown_ = WOLFSSL_RECEIVED_SHUTDOWN;
    this->ssl_send_shutdown_ = WOLFSSL_SENT_SHUTDOWN;
    this->early_data_read_error_ = WOLFSSL_READ_EARLY_DATA_ERROR;
    this->early_data_read_finish_ = WOLFSSL_READ_EARLY_DATA_FINISH;
    this->early_data_read_success_ = WOLFSSL_READ_EARLY_DATA_SUCCESS;
}

manapi::net::worker::WolfSSL_TLS::~WolfSSL_TLS() {
    wolfSSL_CTX_free(static_cast<WOLFSSL_CTX *>(this->ctx));
}

void manapi::net::worker::WolfSSL_TLS::init(std::size_t deep) {
    TLS::init(deep + 1);

    // init
    this->ctx = ssl_create_context(this->config_->tls_version);
    // setup ctx (load certs)
    this->ssl_configure_context();

    this->alpn_protocol_list = "";
    std::vector<int> hlist = {
        http::versions::HTTP_v0_9,
        http::versions::HTTP_v1_0,
        http::versions::HTTP_v1_1,
        http::versions::HTTP_v2,
        http::versions::HTTP_v3
    };
    auto it = hlist->begin();
    goto skip;
    for (; it != hlist->end(); ++it) {
        this->alpn_protocol_list += ",";
        skip:
        switch (*it) {
            case http::versions::HTTP_v0_9:
                this->alpn_protocol_list += "http/0.9";
            break;
            case http::versions::HTTP_v1_0:
                this->alpn_protocol_list += "http/1.0";
            break;
            case http::versions::HTTP_v1_1:
                this->alpn_protocol_list += "http/1.1";
            break;
            case http::versions::HTTP_v2:
                this->alpn_protocol_list += "h2";
            break;
            case http::versions::HTTP_v3:
                this->alpn_protocol_list += "h3";
            break;
        }
    }
}

std::shared_ptr<manapi::net::worker::WolfSSL_TLS> manapi::net::worker::WolfSSL_TLS::create(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::WolfSSL_TLS>(std::move(site), std::move(wdata), config.get());
    worker->self_ = worker;
    return std::move(worker);
}

bool manapi::net::worker::WolfSSL_TLS::ssl_is_init_fininshed_(void *ssl) {
    return wolfSSL_is_init_finished(static_cast<WOLFSSL *>(ssl));
}

int manapi::net::worker::WolfSSL_TLS::ssl_get_error_(void *ssl, int rhs) {
    return wolfSSL_get_error(static_cast<WOLFSSL *>(ssl), rhs);
}

int manapi::net::worker::WolfSSL_TLS::ssl_accept_(void *ssl) {
    return wolfSSL_accept(static_cast<WOLFSSL *>(ssl));
}

void * manapi::net::worker::WolfSSL_TLS::ssl_new_(void *ctx) {
    return wolfSSL_new(static_cast<WOLFSSL_CTX *>(ctx));
}

int manapi::net::worker::WolfSSL_TLS::ssl_write_(void *ssl, const void *buff, int size) {
    return wolfSSL_write(static_cast<WOLFSSL *>(ssl), buff, size);
}

int manapi::net::worker::WolfSSL_TLS::ssl_read_(void *ssl, void *buff, int size) {
    return wolfSSL_read(static_cast<WOLFSSL *>(ssl), buff, size);
}

int manapi::net::worker::WolfSSL_TLS::ssl_shutdown_(void *ssl) {
    return wolfSSL_shutdown(static_cast<WOLFSSL *>(ssl));
}

void manapi::net::worker::WolfSSL_TLS::ssl_set_shutdown_(void *ssl, int flags) {
    return wolfSSL_set_shutdown(static_cast<WOLFSSL *>(ssl), flags);
}

void manapi::net::worker::WolfSSL_TLS::ssl_free_(void *ssl) {
    return wolfSSL_free(static_cast<WOLFSSL *>(ssl));
}

int manapi::net::worker::WolfSSL_TLS::ssl_bio_read_(void *wbio, void *buff, int size) {
    return wolfSSL_BIO_read(static_cast<WOLFSSL_BIO*>(wbio), buff, static_cast<int>(size));
}

int manapi::net::worker::WolfSSL_TLS::ssl_bio_should_retry_(void *bio) {
    return true;
}

int manapi::net::worker::WolfSSL_TLS::ssl_bio_write_(void *rbio, const void *buff, int size) {
    return wolfSSL_BIO_write(static_cast<WOLFSSL_BIO*>(rbio), buff, static_cast<int>(size));
}

int manapi::net::worker::WolfSSL_TLS::ssl_read_early_data_(void *ssl, void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_read_early_data(static_cast<SSL*>(ssl), buf, num, readbytes);
}

int manapi::net::worker::WolfSSL_TLS::ssl_write_early_data_(void *ssl, const void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXCEPT {
    return wolfSSL_write_early_data(static_cast<SSL*>(ssl), buf, num, readbytes);
}

bool manapi::net::worker::WolfSSL_TLS::ssl_early_data_is_enabled_(void *ctx) MANAPIHTTP_NOEXCEPT {
    return true;
}

bool manapi::net::worker::WolfSSL_TLS::recv_setup_connection(connection_interface *data) {
    ERR_clear_error();

    data->wbio = BIO_new(BIO_s_mem());
    if (!data->wbio) {
        goto err;
    }

    data->rbio = BIO_new(BIO_s_mem());
    if (!data->rbio) {
        goto err;
    }

    SSL_set_accept_state(static_cast<SSL*>(data->ssl));

    SSL_set_bio(static_cast<SSL*>(data->ssl), static_cast<BIO*>(data->rbio), static_cast<BIO*>(data->wbio));

    if (WOLFSSL_SUCCESS != wolfSSL_UseALPN(static_cast<WOLFSSL *>(data->ssl),
        this->alpn_protocol_list.data(), this->alpn_protocol_list.size(),
        WOLFSSL_ALPN_FAILED_ON_MISMATCH))
        goto err;

    return true;
    err:
        return false;
}


void * manapi::net::worker::WolfSSL_TLS::ssl_create_context(size_t version) {
    std::string_view ret;
    auto cipher_list = this->config_->get_config_param<std::string>(this->config_->ssl, "cipher_list", {});
    WOLFSSL_METHOD *method;
    WOLFSSL_CTX *ctx;

    switch (version)
    {
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        // ReSharper disable once CppDeprecatedEntity
        case http::versions::TLS_v1_2:    method = wolfTLSv1_2_server_method();   break;
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        case http::versions::TLS_v1_3:    method = wolfTLS_server_method();       break;
        default: THROW_MANAPIHTTP_EXCEPTION(ERR_FAILED_PRECONDITION,
            "can not find the initialization method openssl (tls_version): {}", version);
    }


    ctx = wolfSSL_CTX_new(method);

    if (!ctx)
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL, "{}", "cannot create the openssl context for the tcp connection");

    wolfSSL_CTX_set_cipher_list(ctx, static_cast<const char *>(cipher_list.data()));
    wolfSSL_CTX_set_options(ctx, WOLFSSL_OP_NO_SSLv2);
#if MANAPIHTTP_WOLFSSL_WITH_ALPN
    wolfSSL_CTX_set_session_id_context(ctx, reinterpret_cast<const unsigned char *>(&this->ssl_session_ctx_id), sizeof(this->ssl_session_ctx_id));
#endif

    this->ssl_configure_context();

    return ctx;
err:
    THROW_MANAPIHTTP_EXCEPTION (ERR_FAILED_PRECONDITION, "couldn't setup SSL ctx due to \n{}", ret);
}

void manapi::net::worker::WolfSSL_TLS::ssl_configure_context() {
    auto verify_peer = this->config_->get_config_param<bool>(this->config_->ssl, "verify_peer", true);
    auto cert = this->config_->get_config_param<std::string>(this->config_->ssl, "cert", {});
    auto key = this->config_->get_config_param<std::string>(this->config_->ssl, "key", {});

    if (wolfSSL_CTX_use_certificate_file(static_cast<WOLFSSL_CTX *>(this->ctx), cert.data(), SSL_FILETYPE_PEM) <= 0)
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL, "{}", "cannot use cert file openssl");

    if (wolfSSL_CTX_use_PrivateKey_file(static_cast<WOLFSSL_CTX *>(this->ctx), key.data(), SSL_FILETYPE_PEM) <= 0)
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL, "{}", "cannot use private key file openssl");

    if (!wolfSSL_CTX_check_private_key(static_cast<WOLFSSL_CTX *>(this->ctx)))
        MANAPIHTTP_LOG("Private key does not match the certificate public key.\nCertificate File: {}, Pivate Key File: {}", cert.data(), key.data());

    wolfSSL_CTX_set_verify(static_cast<WOLFSSL_CTX *>(this->ctx), verify_peer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
    wolfSSL_CTX_set_verify_depth(static_cast<WOLFSSL_CTX *>(this->ctx), 1);
}

#endif // MANAPIHTTP_WOLFSSL_DEPENDENCY