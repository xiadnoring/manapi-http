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

struct ssl_bio_deleter_t {
    void operator() (BIO *b) {
        BIO_free(b);
    }
};

manapi::net::worker::OpenSSL_TLS::OpenSSL_TLS(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, manapi::net::http::config *config) : TLS (std::move(site), std::move(wdata), config) {
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

std::shared_ptr<manapi::net::worker::OpenSSL_TLS> manapi::net::worker::OpenSSL_TLS::create(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::OpenSSL_TLS>(std::move(site), std::move(wdata), config.get());
    worker->self_ = worker;
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
    //ERR_clear_error();
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

bool manapi::net::worker::OpenSSL_TLS::recv_setup_connection(connection_interface *data) {
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

    return true;
err:
    return false;
}

void * manapi::net::worker::OpenSSL_TLS::ssl_create_context(const size_t &version) {
    auto cipher_list = this->config_->get_config_param<std::string>(this->config_->ssl, "cipher_list", {});

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


    SSL_CTX_set_max_early_data(ctx, 16384);
    SSL_CTX_clear_options(ctx, SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_min_proto_version(ctx, 0);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);

    // SSL_CTX_set_max_send_fragment(ctx, this->config->buffer_size());
    // SSL_CTX_set_default_read_buffer_len(ctx, this->config->buffer_size());

    //SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2|SSL_OP_NO_TICKET);
    //SSL_CTX_set_session_id_context(ctx, reinterpret_cast<const unsigned char *>(&this->ssl_session_ctx_id), sizeof(this->ssl_session_ctx_id));

    if (single_dh_use)
        SSL_CTX_set_options(ctx, SSL_OP_SINGLE_DH_USE);

    SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS);

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
                *out = reinterpret_cast<const unsigned char *> (buff.data());
                *outlen = buff.size();
                return 0;
            }
        }

        return -1;
    }, this);

    return ctx;
err:
    std::unique_ptr<BIO, ssl_bio_deleter_t> bio;
    bio.reset(BIO_new(BIO_s_mem()));
    ERR_print_errors(bio.get());
    char *buf;
    size_t len = BIO_get_mem_data(bio.get(), &buf);

    std::string ret(buf, len);

    THROW_MANAPIHTTP_EXCEPTION (ERR_FAILED_PRECONDITION, "couldn't setup SSL ctx due to \n{}", ret);
}

void manapi::net::worker::OpenSSL_TLS::ssl_configure_context() {
    ERR_clear_error();

    auto verify_peer = this->config_->get_config_param<bool>(this->config_->ssl, "verify_peer", true);
    auto cert = this->config_->get_config_param<std::string>(this->config_->ssl, "cert", {});
    auto key = this->config_->get_config_param<std::string>(this->config_->ssl, "key", {});

    if (SSL_CTX_use_certificate_file(static_cast<SSL_CTX*>(this->ctx), cert.data(), SSL_FILETYPE_PEM) <= 0)
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL, "{}", "cannot use cert file openssl");


    if (SSL_CTX_use_PrivateKey_file(static_cast<SSL_CTX*>(this->ctx), key.data(), SSL_FILETYPE_PEM) <= 0)
        THROW_MANAPIHTTP_EXCEPTION(ERR_INTERNAL, "{}", "cannot use private key file openssl");


    if (!SSL_CTX_check_private_key(static_cast<SSL_CTX*>(this->ctx)))
        MANAPIHTTP_LOG("Private key does not match the certificate public key.\nCertificate File: {}, Pivate Key File: {}", cert.data(), key.data());

    SSL_CTX_set_verify(static_cast<SSL_CTX*>(this->ctx), verify_peer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_verify_depth(static_cast<SSL_CTX*>(this->ctx), 1);
}



#endif // MANAPIHTTP_OPENSSL_DEPENDENCY