#pragma once

#if MANAPIHTTP_OPENSSL_DEPENDENCY

#include <openssl/ssl.h>
#include <openssl/err.h>

/* OpenSSL >=3.4.0 */
#if OPENSSL_VERSION_NUMBER >= 809500672
#define MANAPI_OPENSSL_QUIC_REALIZATION

#include "./base_worker.hpp"
#include "./UDP.hpp"

namespace manapi::net::worker {
    class openssl_quic : public udp {
    public:
        explicit openssl_quic(net::site &site);
        ~openssl_quic() override;
        void init() override;
        static std::shared_ptr<worker::openssl_quic> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);
        void onrecv(ev::io &watcher, int revents) override;
    private:
        SSL_CTX *ctx{nullptr};
    };
}

#endif
#endif