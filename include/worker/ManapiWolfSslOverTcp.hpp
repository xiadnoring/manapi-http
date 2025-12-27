#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiTlsOverTcp.hpp"

#if MANAPIHTTP_WOLFSSL_DEPENDENCY

namespace manapi::net::worker {
    class WolfSSL_TLS : public worker::TLS {
    public:
        WolfSSL_TLS (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config);

        ~WolfSSL_TLS () override;

        manapi::future<manapi::status> init(std::size_t deep) override;

        static std::shared_ptr<worker::WolfSSL_TLS> create (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config* config);

        void stop(std::function<void()> cb) override;
    protected:

        bool ssl_is_init_fininshed_ (void *ssl) MANAPIHTTP_NOEXCEPT override;

        int ssl_get_error_ (void *ssl, int rhs) MANAPIHTTP_NOEXCEPT override;

        int ssl_accept_ (void *ssl) MANAPIHTTP_NOEXCEPT override;

        void *ssl_new_ (void *ctx) MANAPIHTTP_NOEXCEPT override;

        int ssl_write_ (void *ssl, const void *buff, int size) MANAPIHTTP_NOEXCEPT override;

        int ssl_read_ (void *ssl, void *buff, int size) MANAPIHTTP_NOEXCEPT override;

        int ssl_shutdown_ (void *ssl) MANAPIHTTP_NOEXCEPT override;

        void ssl_set_shutdown_(void *ssl, int flags) MANAPIHTTP_NOEXCEPT override;

        void ssl_free_(void *ssl) MANAPIHTTP_NOEXCEPT override;

        int ssl_bio_read_(void *wbio, void *buff, int size) MANAPIHTTP_NOEXCEPT override;

        int ssl_bio_should_retry_(void *bio) MANAPIHTTP_NOEXCEPT override;

        int ssl_bio_write_(void *rbio, const void *buff, int size) MANAPIHTTP_NOEXCEPT override;

        int ssl_read_early_data_(void *ssl, void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXCEPT override;

        int ssl_write_early_data_(void *ssl, const void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXCEPT override ;

        bool ssl_early_data_is_enabled_(void *ctx) MANAPIHTTP_NOEXCEPT override;

        int recv_setup_connection(const shared_conn &conn, tls_connection_t *data) MANAPIHTTP_NOEXCEPT override;

        manapi::status_or<void *> ssl_create_context (size_t version) MANAPIHTTP_NOEXCEPT;

        manapi::status ssl_configure_context (void *ctx, http::server_ctx::pool_t *pool_data, std::size_t deeplvl) MANAPIHTTP_NOEXCEPT;
    private:
        http::server_ctx::pool_t *pool_data_;
    };
}
#endif