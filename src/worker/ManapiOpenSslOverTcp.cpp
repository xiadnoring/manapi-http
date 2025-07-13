#include "worker/ManapiOpenSslOverTcp.hpp"

#include "ManapiParams.hpp"
#include "async/ManapiAsyncSocket.hpp"
#include "ManapiInitTools.hpp"
#include "../include/ManapiUtils.hpp"

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
}

manapi::net::worker::OpenSSL_TLS::~OpenSSL_TLS() {
    SSL_CTX_free(static_cast<SSL_CTX*>(this->ctx));
}

std::shared_ptr<manapi::net::worker::OpenSSL_TLS> manapi::net::worker::OpenSSL_TLS::create(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<worker::OpenSSL_TLS>(std::move(site), std::move(wdata), config.get());
    worker->self_ = worker;
    return std::move(worker);
}

void manapi::net::worker::OpenSSL_TLS::stop(std::function<void()> cb) {
    this->cache_cleaner.stop();
    this->cache_cleaner = nullptr;
    TLS::stop(std::move(cb));
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
    if (!data->wbio)
        goto err;

    data->rbio = BIO_new(BIO_s_mem());
    if (!data->rbio)
        goto err;

    SSL_set_accept_state(static_cast<SSL*>(data->ssl));

    SSL_set_bio(static_cast<SSL*>(data->ssl), static_cast<BIO*>(data->rbio), static_cast<BIO*>(data->wbio));

    return true;
err:
    return false;
}

void * manapi::net::worker::OpenSSL_TLS::ssl_create_context(size_t version) {
    auto const cipher_list = this->config_->get_config_param<std::string>(this->config_->ssl, "ciphers", {});
    auto const ssl_v2 = this->config_->get_config_param<bool>(this->config_->ssl, "ssl_v2", false);
    auto const ssl_v3 = this->config_->get_config_param<bool>(this->config_->ssl, "ssl_v3", true);
    auto const ticket = this->config_->get_config_param<bool>(this->config_->ssl, "ticket", true);
    auto const sess_timeout = this->config_->get_config_param<uint32_t>(this->config_->ssl, "sess_timeout", 300);
    auto const sess_cache = this->config_->get_config_param<bool>(this->config_->ssl, "sess_cache", false);
    auto const sess_cache_size = this->config_->get_config_param<uint32_t>(this->config_->ssl, "sess_cache_size", 1024 * 20);

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
        default: THROW_MANAPIHTTP_EXCEPTION(ERR_AI_FAILED_PRECONDITION,
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


    //SSL_CTX_set_max_early_data(ctx, 16384);
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
        SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);

        if (!SSL_CTX_set_session_id_context(ctx,
            reinterpret_cast<const unsigned char *>(&this->ssl_session_ctx_id), sizeof(this->ssl_session_ctx_id)))
            goto err;
    }
    else {
        SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
    }

    //long cache_mode = SSL_SESS_CACHE_SERVER;
    SSL_CTX_sess_set_cache_size(ctx, sess_cache_size);

    if (!SSL_CTX_set_timeout(ctx, sess_timeout))
        goto err;

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

    this->cache_cleaner = manapi::async::current()->timerpool()->append_timer_sync(
        10000, [ctx](manapi::timer t)->void {
        SSL_CTX_flush_sessions_ex(ctx, ::time(0));
    });

    return ctx;
err:
    std::unique_ptr<BIO, ssl_bio_deleter_t> bio;
    bio.reset(BIO_new(BIO_s_mem()));
    ERR_print_errors(bio.get());
    char *buf;
    size_t len = BIO_get_mem_data(bio.get(), &buf);

    std::string ret(buf, len);

    THROW_MANAPIHTTP_EXCEPTION (ERR_AI_FAILED_PRECONDITION, "couldn't setup SSL ctx due to \n{}", ret);
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