#pragma once

#include "../ManapiUtils.hpp"
#include "../ManapiSite.hpp"
#include "./QUIC_CB_Base.hpp"

// #if MANAPIHTTP_OPENSSL_DEPENDENCY
//
// #define MANAPIHTTP_DEFAULT_QUIC
//
// #include <openssl/ssl.h>
//
// namespace manapi::net::worker {
//     // This worker will be unique for every connection
//     class quic_openssl_tls : public quic_cb_base {
//     public:
//         explicit quic_openssl_tls(net::site &site);
//         ~quic_openssl_tls() override;
//
//         static void ssl_configure_context (SSL_CTX *ctx);
//         static void global_init (net::site &site);
//         static void global_deinit (net::site &site);
//
//         future<void> client_application(quic_frame_data_t frame) override;
//         future<void> client_handshake(quic_frame_data_t frame) override;
//         future<void> client_handshake_finished(quic_frame_data_t frame, std::string &server_handshake_finished) override;
//         future<void> client_init(quic_frame_data_t frame, std::string &server_hello, std::string &server_handshake) override;
//         future<void> client_init_ack(quic_frame_data_t frame) override;
//
//     private:
//         static std::mutex ctx_init_mx;
//         static std::unique_ptr <async::mutex> ctx_mx;
//         static SSL_CTX *ctx;
//
//         void quic_write_data (std::string_view data);
//
//         future<void> _init_ssl ();
//         future<void> _setup_ssl ();
//         future<void> _deinit_ssl ();
//
//         SSL *ssl{nullptr};
//         BIO *wbio{nullptr};
//         BIO *rbio{nullptr};
//         uint8_t buf_bio[64];
//
//         std::deque<std::string> wbuf;
//         std::string rbuf;
//     };
// }
//
//
// #endif