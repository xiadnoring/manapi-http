#pragma once

#include "../ManapiUtils.hpp"
#include "./TLS.hpp"

#if MANAPIHTTP_WOLFSSL_DEPENDENCY

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

#include "./base_worker.hpp"
#include "./TCP.hpp"

namespace manapi::net::worker {
    class WolfSSL_TLS : public worker::TLS {
    public:
        WolfSSL_TLS (net::site &site);
        ~WolfSSL_TLS ();
        void init() override;
        static std::shared_ptr<worker::WolfSSL_TLS> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);
    protected:
        bool ssl_is_init_fininshed_ (void *ssl) override;
        int ssl_get_error_ (void *ssl, int rhs) override;
        int ssl_accept_ (void *ssl) override;
        void *ssl_new_ (void *ctx) override;
        int ssl_write_ (void *ssl, const void *buff, int size) override;
        int ssl_read_ (void *ssl, void *buff, int size) override;
        int ssl_shutdown_ (void *ssl) override;
        void ssl_set_shutdown_(void *ssl, int flags) override;
        void ssl_free_(void *ssl) override;

        void recv_setup_connection(manapi::net::worker::connection &storage) override;
        void* ssl_create_context (const size_t &version) override;
        void ssl_configure_context () override;

        std::string alpn_protocol_list;
    };
}
#endif