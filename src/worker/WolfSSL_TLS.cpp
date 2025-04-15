#include "worker/WolfSSL_TLS.hpp"

#include "ManapiString.hpp"
#include "ManapiUtils.hpp"

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
#include "http/HeaderView.hpp"
#include "ManapiInitTools.hpp"
#include "async/ManapiAsyncSocket.hpp"

manapi::net::worker::WolfSSL_TLS::WolfSSL_TLS(net::site &site) : TLS (site) {
    this->ssl_error_none_ = WOLFSSL_ERROR_NONE;
    this->ssl_error_syscall_ = WOLFSSL_ERROR_SYSCALL;
    this->ssl_error_want_read_ = WOLFSSL_ERROR_WANT_READ;
    this->ssl_error_want_write_ = WOLFSSL_ERROR_WANT_WRITE;
    this->ssl_error_zero_return_ = WOLFSSL_ERROR_ZERO_RETURN;
    this->ssl_error_ssl_ = WOLFSSL_ERROR_SSL;
    this->ssl_recv_shutdown_ = WOLFSSL_RECEIVED_SHUTDOWN;
    this->ssl_send_shutdown_ = WOLFSSL_SENT_SHUTDOWN;

    init_tools::ssl_library_init();
}

manapi::net::worker::WolfSSL_TLS::~WolfSSL_TLS() {
    wolfSSL_CTX_free(static_cast<WOLFSSL_CTX *>(this->ctx));
}

void manapi::net::worker::WolfSSL_TLS::init() {
    TLS::init();

    auto sslconfig = config->get_ssl_config();
    if (sslconfig->enabled) {
        // init
        ctx = ssl_create_context(config->get_tls_version());
        // setup ctx (load certs)
        ssl_configure_context();

        this->write = [this](auto &PH1, auto PH2, auto PH3, auto PH4)
            -> future<ssize_t> { return ssl_write(PH1, PH2, PH3); };

        this->read = [this](auto &PH1, auto PH2, auto PH3)
            -> future<ssize_t> { return ssl_read(PH1, PH2, PH3); };
    }

    this->alpn_protocol_list = "";
    auto http_versions = this->config->http_versions().get();
    auto it = http_versions->begin();
    goto skip;
    for (; it != http_versions->end(); ++it) {
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

std::shared_ptr<manapi::net::worker::WolfSSL_TLS> manapi::net::worker::WolfSSL_TLS::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::WolfSSL_TLS>(site);
    worker->set_config(std::move(config));
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

void manapi::net::worker::WolfSSL_TLS::recv_setup_connection(manapi::net::worker::connection &storage) {
    auto &conn_data = storage.as<connection_interface>();
    wolfSSL_set_fd(static_cast<WOLFSSL*>(conn_data.ssl), conn_data.id);
    conn_data.status.fetch_or(CONN_IDLE);
}


void * manapi::net::worker::WolfSSL_TLS::ssl_create_context(const size_t &version) {
    WOLFSSL_METHOD *method;
    WOLFSSL_CTX *ctx;

    switch (version)
    {
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        // ReSharper disable once CppDeprecatedEntity
        case http::versions::TLS_v1_2:    method = wolfTLSv1_2_server_method();   break;
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        case http::versions::TLS_v1_3:    method = wolfTLS_server_method();       break;
        default: THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR,
            "can not find the initialization method openssl (tls_version): {}", version);
    }


    ctx = wolfSSL_CTX_new(method);

    if (!ctx)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot create the openssl context for the tcp connection");
    }

    //SSL_CTX_set_mode(ctx, SSL_MODE_ASYNC);

    // SSL_CTX_set_max_send_fragment(ctx, this->config->buffer_size());
    // SSL_CTX_set_default_read_buffer_len(ctx, this->config->buffer_size());
    auto cipher_list = this->config->cipher_list();
    wolfSSL_CTX_set_cipher_list(ctx, static_cast<const char *>(cipher_list->data()));
    wolfSSL_CTX_set_options(ctx, WOLFSSL_OP_NO_SSLv2);
#if MANAPIHTTP_WOLFSSL_WITH_ALPN
    wolfSSL_CTX_set_session_id_context(ctx, reinterpret_cast<const unsigned char *>(&this->ssl_session_ctx_id), sizeof(this->ssl_session_ctx_id));
#endif

    return ctx;
}

void manapi::net::worker::WolfSSL_TLS::ssl_configure_context() {
    auto sslconfig = this->config->get_ssl_config();
    if (wolfSSL_CTX_use_certificate_file(static_cast<WOLFSSL_CTX *>(this->ctx), sslconfig->cert.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use cert file openssl");
    }

    if (wolfSSL_CTX_use_PrivateKey_file(static_cast<WOLFSSL_CTX *>(this->ctx), sslconfig->key.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use private key file openssl");
    }

    if (!wolfSSL_CTX_check_private_key(static_cast<WOLFSSL_CTX *>(this->ctx))) {
        MANAPIHTTP_LOG(this->site.async_context(), "Private key does not match the certificate public key.\nCertificate File: {}, Pivate Key File: {}", sslconfig->cert.data(), sslconfig->key.data());
    }

    wolfSSL_CTX_set_verify(static_cast<WOLFSSL_CTX *>(this->ctx), this->config->get_verify_peer().load() ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
    wolfSSL_CTX_set_verify_depth(static_cast<WOLFSSL_CTX *>(this->ctx), 1);
}

#endif // MANAPIHTTP_WOLFSSL_DEPENDENCY