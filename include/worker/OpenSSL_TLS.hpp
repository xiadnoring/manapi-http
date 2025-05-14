#pragma once

#include "../ManapiUtils.hpp"
#include "./TLS.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

#include "./base_worker.hpp"
#include "./TCP.hpp"

namespace manapi::net::worker {
    class OpenSSL_TLS : public worker::TLS {
    public:
        OpenSSL_TLS (net::http::site site, std::shared_ptr<worker::worker_config_t> wdata);
        ~OpenSSL_TLS ();
        static std::shared_ptr<worker::OpenSSL_TLS> create (net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, std::shared_ptr<manapi::net::http::config> config);
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
        int ssl_bio_read_(void *wbio, void *buff, int size) override;
        int ssl_bio_write_(void *rbio, const void *buff, int size) override;
        int ssl_bio_should_retry_(void *bio) override;

        bool recv_setup_connection(connection_interface *storage) override;
        void* ssl_create_context (const size_t &version) override;
        void ssl_configure_context () override;
    };
}
#endif