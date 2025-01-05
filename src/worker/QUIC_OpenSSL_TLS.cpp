#include "worker/QUIC_OpenSSL_TLS.hpp"

#include <openssl/err.h>

#include "crypto/ManapiCryptoUtils.hpp"
#include "worker/QUIC.hpp"

std::mutex manapi::net::worker::quic_openssl_tls::ctx_init_mx{};
SSL_CTX *manapi::net::worker::quic_openssl_tls::ctx{nullptr};
std::unique_ptr<manapi::async_mutex> manapi::net::worker::quic_openssl_tls::ctx_mx{nullptr};

manapi::net::worker::quic_openssl_tls::quic_openssl_tls(net::site &site) : quic_cb_base(site) {

}

manapi::net::worker::quic_openssl_tls::~quic_openssl_tls() {

}

void manapi::net::worker::quic_openssl_tls::global_init(net::site &site) {
    std::lock_guard<std::mutex> lk (quic_openssl_tls::ctx_init_mx);

    if (!quic_openssl_tls::ctx_mx) {
        quic_openssl_tls::ctx_mx = std::make_unique<async_mutex>(site.taskpool);
    }
    if (!quic_openssl_tls::ctx) {
        SSL_load_error_strings();
        ERR_load_crypto_strings();

        quic_openssl_tls::ctx = SSL_CTX_new(TLS_server_method());

        ssl_configure_context(ctx);
    }
}

void manapi::net::worker::quic_openssl_tls::global_deinit(net::site &site) {
    std::lock_guard<std::mutex> lk (quic_openssl_tls::ctx_init_mx);

    if (quic_openssl_tls::ctx_mx) {
        quic_openssl_tls::ctx_mx.reset();
    }

    if (quic_openssl_tls::ctx) {
        SSL_CTX_free(std::exchange(quic_openssl_tls::ctx, nullptr));
    }
}

manapi::future<void> manapi::net::worker::quic_openssl_tls::client_application(quic_frame_data_t frame) {

    co_return;
}

manapi::future<void> manapi::net::worker::quic_openssl_tls::client_handshake(quic_frame_data_t frame) {

    co_return;
}

manapi::future<void> manapi::net::worker::quic_openssl_tls::client_handshake_finished(quic_frame_data_t frame, std::string &server_handshake_finished) {
    co_return;
}

void manapi::net::worker::quic_openssl_tls::ssl_configure_context(SSL_CTX *ctx) {
    if (SSL_CTX_use_certificate_file(ctx, "/home/Timur/Documents/ssl/quic/cert.crt", SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use cert file openssl");
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "/home/Timur/Documents/ssl/quic/cert.key", SSL_FILETYPE_PEM) <= 0)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_EXTERNAL_LIB_CRASH, "{}", "cannot use private key file openssl");
    }
}

manapi::future<void> manapi::net::worker::quic_openssl_tls::client_init(quic_frame_data_t frame, std::string &server_hello, std::string &server_handshake) {
    co_await this->_init_ssl();
    co_await this->_setup_ssl();

    std::string data;
    data.reserve(frame.data.size());

    auto max_size = static_cast<ssize_t>(frame.data.size());
    ssize_t i = 8;

    data += quic::_parse_string(frame.data, i, 2); // TLS version
    data += quic::_parse_string(frame.data, i, 32); // Client Random
    data += quic::_parse_string(frame.data, i, 1); // session id
    //data += crypto::strhex2strdec(std::string{"20e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"});
    auto cipher_suites_len = quic::_parse_number<uint16_t>(frame.data, i);
    data += crypto::number2bytes<uint16_t>(cipher_suites_len);
    data += quic::_parse_string(frame.data, i, cipher_suites_len); // cipher suites
    data += quic::_parse_string(frame.data, i, 2); // compression method
    auto extensions_length = quic::_parse_number<uint16_t>(frame.data, i);

    auto rvalue = i + extensions_length;
    std::string extensions;
    extensions.reserve(extensions_length);

    for (; i < rvalue;) {
        auto ext_type = quic::_parse_string(frame.data, i, 2);
        auto ext_len = quic::_parse_number<uint16_t>(frame.data, i);
        auto ext_data = quic::_parse_string(frame.data, i, ext_len);

        extensions += ext_type;
        extensions += crypto::number2bytes<uint16_t>(ext_len);
        extensions += ext_data;
        if (ext_type != std::string{"\0009", 2}) {
        }
        else {
            printf("quic data!\n");
        }
    }
    data += crypto::number2bytes<uint16_t>(extensions.size()); // extensions length
    data += extensions;

    data = std::string{"\001\000", 2} + crypto::number2bytes<uint16_t>(data.size()) + data;
    data = crypto::strhex2strdec(std::string{"160301"}) + crypto::number2bytes<uint16_t>(data.size()) + data;

    auto rhs = BIO_write(this->rbio, data.data(), static_cast<int>(data.size()));
    if (rhs < 1) {
        auto want_read = BIO_pending(this->rbio);
        auto want_write = BIO_pending(this->wbio);

        rhs = SSL_get_error(this->ssl, rhs);
        printf("Hello: %s\n", ERR_error_string(rhs, nullptr));

        co_return;
    }

    std::cout << crypto::strdec2strhex(data) << "\n";

    rhs = SSL_accept(this->ssl);

    if (rhs < 1) {
        auto want_read = BIO_pending(this->rbio);
        auto want_write = BIO_pending(this->wbio);

        rhs = SSL_get_error(this->ssl, rhs);

        if (rhs == 2) {
            if (!this->wbuf.empty()) {
                server_hello = std::move(this->wbuf.front());
                this->wbuf.pop_front();
            }

            while (!this->wbuf.empty()) {
                server_handshake += this->wbuf.front();
                this->wbuf.pop_front();
            }

            co_return;
        }

        co_return;
    }

    co_return;
}

manapi::future<void> manapi::net::worker::quic_openssl_tls::client_init_ack(quic_frame_data_t frame) {

    co_return;
}

void manapi::net::worker::quic_openssl_tls::quic_write_data(std::string_view data) {
    this->wbuf.emplace_back(data);
}

manapi::future<> manapi::net::worker::quic_openssl_tls::_init_ssl() {
    auto lk = co_await quic_openssl_tls::ctx_mx->lock_guard();
    this->ssl = SSL_new(quic_openssl_tls::ctx);
}

manapi::future<> manapi::net::worker::quic_openssl_tls::_setup_ssl() {
    SSL_set_blocking_mode(this->ssl, 0);
    /* early data is not supported */
    SSL_set_max_early_data(this->ssl, 0);
    SSL_set_msg_callback_arg(this->ssl, this);
    SSL_set_msg_callback(this->ssl, [] (int write_p, int version, int content_type, const void *buf, size_t len, SSL *ssl, void *argp) -> void {
        auto worker = static_cast<quic_openssl_tls *> (argp);
        if (write_p) {
            std::string_view data (static_cast <const char*>(buf), len);
            switch (content_type) {
                case SSL3_RT_HANDSHAKE: {
                    worker->quic_write_data(data);
                    break;
                }
                default:
                    break;
            }
        }
    });

    this->rbio = BIO_new(BIO_s_mem());
    this->wbio = BIO_new(BIO_s_null());

    SSL_set_accept_state(this->ssl);

    SSL_set_bio(this->ssl, this->rbio, this->wbio);
    co_return;
}

manapi::future<> manapi::net::worker::quic_openssl_tls::_deinit_ssl() {
    auto lk = co_await quic_openssl_tls::ctx_mx->lock_guard();
    SSL_free(this->ssl);
    co_return;
}
