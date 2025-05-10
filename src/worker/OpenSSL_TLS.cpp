#include "worker/OpenSSL_TLS.hpp"

#include "ManapiParams.hpp"
#include "async/ManapiAsyncSocket.hpp"
#include "ManapiInitTools.hpp"

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

#include "ManapiUtils.hpp"
#include "http/HeaderView.hpp"

manapi::net::worker::OpenSSL_TLS::OpenSSL_TLS(net::site &site) : TLS (site) {
    this->ssl_error_none_ = SSL_ERROR_NONE;
    this->ssl_error_syscall_ = SSL_ERROR_SYSCALL;
    this->ssl_error_want_read_ = SSL_ERROR_WANT_READ;
    this->ssl_error_want_write_ = SSL_ERROR_WANT_WRITE;
    this->ssl_error_zero_return_ = SSL_ERROR_ZERO_RETURN;
    this->ssl_error_ssl_ = SSL_ERROR_SSL;
    this->ssl_recv_shutdown_ = SSL_RECEIVED_SHUTDOWN;
    this->ssl_send_shutdown_ = SSL_SENT_SHUTDOWN;

    init_tools::ssl_library_init();
}

manapi::net::worker::OpenSSL_TLS::~OpenSSL_TLS() {
    SSL_CTX_free(static_cast<SSL_CTX*>(this->ctx));
}

std::shared_ptr<manapi::net::worker::OpenSSL_TLS> manapi::net::worker::OpenSSL_TLS::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::OpenSSL_TLS>(site);
    worker->config(std::move(config));
    return std::move(worker);
}

bool manapi::net::worker::OpenSSL_TLS::ssl_is_init_fininshed_(void *ssl) {
    ERR_clear_error();
    return SSL_is_init_finished(static_cast<SSL*>(ssl));
}

int manapi::net::worker::OpenSSL_TLS::ssl_get_error_(void *ssl, int rhs) {
    return SSL_get_error(static_cast<SSL*>(ssl), rhs);
}

int manapi::net::worker::OpenSSL_TLS::ssl_accept_(void *ssl) {
    ERR_clear_error();
    return SSL_accept(static_cast<SSL*>(ssl));
}

void * manapi::net::worker::OpenSSL_TLS::ssl_new_(void *ctx) {
    ERR_clear_error();
    return SSL_new(static_cast<SSL_CTX*>(ctx));
}

int manapi::net::worker::OpenSSL_TLS::ssl_write_(void *ssl, const void *buff, int size) {
    ERR_clear_error();
    return SSL_write(static_cast<SSL*>(ssl), buff, size);
}

int manapi::net::worker::OpenSSL_TLS::ssl_read_(void *ssl, void *buff, int size) {
    ERR_clear_error();
    return SSL_read(static_cast<SSL*>(ssl), buff, size);
}

int manapi::net::worker::OpenSSL_TLS::ssl_shutdown_(void *ssl) {
    ERR_clear_error();
    return SSL_shutdown(static_cast<SSL*>(ssl));
}

void manapi::net::worker::OpenSSL_TLS::ssl_set_shutdown_(void *ssl, int flags) {
    SSL_set_shutdown(static_cast<SSL*>(ssl), flags);
}

void manapi::net::worker::OpenSSL_TLS::ssl_free_(void *ssl) {
    SSL_free(static_cast<SSL*>(ssl));
}

int manapi::net::worker::OpenSSL_TLS::ssl_bio_read_(void *wbio, void *buff, int size) {
    return BIO_read(static_cast<BIO*>(wbio), buff, static_cast<int>(size));
}

int manapi::net::worker::OpenSSL_TLS::ssl_bio_write_(void *rbio, const void *buff, int size) {
    return BIO_write(static_cast<BIO*>(rbio), buff, static_cast<int>(size));
}

int manapi::net::worker::OpenSSL_TLS::ssl_bio_should_retry_(void *bio) {
    return BIO_should_retry(static_cast<BIO*>(bio));
}

bool manapi::net::worker::OpenSSL_TLS::recv_setup_connection(manapi::net::worker::connection *storage) {
    ERR_clear_error();

    auto conn_data = storage->as<connection_interface>();
    conn_data->status |= CONN_IDLE;

    conn_data->wbio = BIO_new(BIO_s_mem());
    if (!conn_data->wbio) {
        goto err;
    }

    conn_data->rbio = BIO_new(BIO_s_mem());
    if (!conn_data->rbio) {
        goto err;
    }

    if (!SSL_set_blocking_mode(static_cast<SSL*>(conn_data->ssl), 0)) {
        goto err;
    }

    SSL_set_accept_state(static_cast<SSL*>(conn_data->ssl));

    SSL_set_bio(static_cast<SSL*>(conn_data->ssl), static_cast<BIO*>(conn_data->rbio), static_cast<BIO*>(conn_data->wbio));

    return true;
err:
    return false;
}

void * manapi::net::worker::OpenSSL_TLS::ssl_create_context(const size_t &version) {
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
        default: THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR,
            "can not find the initialization method openssl (tls_version): {}", version);
    }


    ERR_clear_error();
    ctx = SSL_CTX_new(method);

    if (!ctx)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot create the openssl context for the tcp connection");
    }

    //SSL_CTX_set_mode(ctx, SSL_MODE_ASYNC);

    // SSL_CTX_set_max_send_fragment(ctx, this->config->buffer_size());
    // SSL_CTX_set_default_read_buffer_len(ctx, this->config->buffer_size());

    //SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2|SSL_OP_NO_TICKET);
    //SSL_CTX_set_session_id_context(ctx, reinterpret_cast<const unsigned char *>(&this->ssl_session_ctx_id), sizeof(this->ssl_session_ctx_id));

    auto cipher_list = this->config()->cipher_list();
    if (!SSL_CTX_set_cipher_list(ctx, static_cast<const char *>(cipher_list->data()))) {
        goto err;
    }

    SSL_CTX_set_alpn_select_cb(ctx, [] (SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
        unsigned int inlen, void *arg) -> int {
        auto worker = static_cast<OpenSSL_TLS *> (arg);
        std::vector <std::string> wishs;
        {
            auto b = worker->config()->http_versions().get();
            for (const auto &version : *b) {
                switch (version) {
                    case http::versions::HTTP_v1_0:
                        wishs.emplace_back("http/1.0");
                    break;
                    case http::versions::HTTP_v0_9:
                        wishs.emplace_back("http/0.9");
                    break;
                    case http::versions::HTTP_v1_1:
                        wishs.emplace_back("http/1.1");
                    break;
                    case http::versions::HTTP_v2:
                        wishs.emplace_back("h2");
                    break;
                    case http::versions::HTTP_v3:
                        wishs.emplace_back("h3");
                    break;
                    default:
                        break;
                }
            }
        }

        std::map <std::string_view, int > exists;
        int j = 0;
        for (int i = 0; i < inlen;j++) {
            int plen = in[i++];
            std::string_view buff (reinterpret_cast<const char *>(in) + i, reinterpret_cast<const char *>(in) + i + plen);
            exists.insert({buff, j});
            i += plen;
        }
        for (const auto &wish: wishs) {
            auto it = exists.find(wish);
            if (it != exists.end()) {
                *out = reinterpret_cast<const unsigned char *> (it->first.data());
                *outlen = it->first.size();
                return 0;
            }
        }
        return -1;
    }, this);

    return ctx;
err:
    THROW_MANAPIHTTP_EXCEPTION2 (ERR_SSL_CONNECTION, "couldn't setup SSL ctx due to error");
}

void manapi::net::worker::OpenSSL_TLS::ssl_configure_context() {
    ERR_clear_error();
    auto sslconfig = this->config()->ssl_config();
    if (SSL_CTX_use_certificate_file(static_cast<SSL_CTX*>(this->ctx), sslconfig->cert.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use cert file openssl");
    }

    if (SSL_CTX_use_PrivateKey_file(static_cast<SSL_CTX*>(this->ctx), sslconfig->key.data(), SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use private key file openssl");
    }

    if (!SSL_CTX_check_private_key(static_cast<SSL_CTX*>(this->ctx))) {
        MANAPIHTTP_LOG("Private key does not match the certificate public key.\nCertificate File: {}, Pivate Key File: {}", sslconfig->cert.data(), sslconfig->key.data());
    }

    SSL_CTX_set_verify(static_cast<SSL_CTX*>(this->ctx), this->config()->verify_peer().load() ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_verify_depth(static_cast<SSL_CTX*>(this->ctx), 1);
}



#endif // MANAPIHTTP_OPENSSL_DEPENDENCY