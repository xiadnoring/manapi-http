#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiTlsOverTcp.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

namespace manapi::net::worker {
    class OpenSSL_TLS : public worker::TLS {
    public:
        OpenSSL_TLS (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config);

        ~OpenSSL_TLS ();

        static std::shared_ptr<worker::OpenSSL_TLS> create (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config);

        void stop(std::function<void()> cb) override;

        manapi::future<manapi::error::status> init(std::size_t deep) override;

        http::server_ctx::pool_t *openssl_pool_data_ () MANAPIHTTP_NOEXPECT;
    protected:
        bool ssl_is_init_fininshed_ (void *ssl) MANAPIHTTP_NOEXPECT override;
        int ssl_get_error_ (void *ssl, int rhs) MANAPIHTTP_NOEXPECT override;
        int ssl_accept_ (void *ssl) MANAPIHTTP_NOEXPECT override;
        void *ssl_new_ (void *ctx) MANAPIHTTP_NOEXPECT override;
        int ssl_write_ (void *ssl, const void *buff, int size) MANAPIHTTP_NOEXPECT override;
        int ssl_read_ (void *ssl, void *buff, int size) MANAPIHTTP_NOEXPECT override;
        int ssl_shutdown_ (void *ssl) MANAPIHTTP_NOEXPECT override;
        void ssl_set_shutdown_(void *ssl, int flags) MANAPIHTTP_NOEXPECT override;
        void ssl_free_(void *ssl) MANAPIHTTP_NOEXPECT override;
        int ssl_bio_read_(void *wbio, void *buff, int size) MANAPIHTTP_NOEXPECT override;
        int ssl_bio_write_(void *rbio, const void *buff, int size) MANAPIHTTP_NOEXPECT override;
        int ssl_bio_should_retry_(void *bio) MANAPIHTTP_NOEXPECT override;
        int ssl_read_early_data_(void *ssl, void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXPECT override;
        int ssl_write_early_data_(void *ssl, const void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXPECT override;
        bool ssl_early_data_is_enabled_(void *ctx) MANAPIHTTP_NOEXPECT override;
        bool recv_setup_connection(tls_connection_t *storage) override;
        manapi::error::status_or<void*> ssl_create_context (size_t version);
        manapi::error::status ssl_configure_context (void* ctx);
    private:
        http::server_ctx::pool_t *pool_data_;
    };
}
#endif