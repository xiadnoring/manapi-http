#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiBaseWorker.hpp"
#include "./ManapiTcp.hpp"

namespace manapi::net::worker {
    struct tls_connection_t;

    class TLS : public worker::TCP {
    public:

        TLS (std::shared_ptr<net::worker::site> site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config);

        ~TLS () override;

        manapi::future<status> init (std::size_t deep) override;

        shared_conn accept (const ev::shared_tcp &w) MANAPIHTTP_NOEXCEPT override;

        void close_connection(shared_conn conn, int flags) MANAPIHTTP_NOEXCEPT override;

        ssize_t sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, std::size_t size, bool finish, std::size_t maxcnt) MANAPIHTTP_NOEXCEPT override;

        ssize_t sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT override;

        int event_flags(const shared_conn & conn, int flags) MANAPIHTTP_NOEXCEPT override;


    protected:
        char ssl_error_none_, ssl_error_want_read_,
        ssl_error_want_write_, ssl_error_zero_return_,
        ssl_error_ssl_, ssl_error_syscall_,
        ssl_send_shutdown_, ssl_recv_shutdown_;
        char early_data_read_finish_, early_data_read_error_, early_data_read_success_;
        char ssl_shutdown_sucess, ssl_shutdown_fatal_error, ssl_shutdown_not_done;

        virtual bool ssl_is_init_fininshed_ (void *ssl) MANAPIHTTP_NOEXCEPT = 0;

        virtual int ssl_get_error_ (void *ssl, int rhs) MANAPIHTTP_NOEXCEPT = 0;

        virtual int ssl_accept_ (void *ssl) MANAPIHTTP_NOEXCEPT = 0;

        virtual void *ssl_new_ (void *ctx) MANAPIHTTP_NOEXCEPT = 0;

        virtual int ssl_write_ (void *ssl, const void *buff, int size) MANAPIHTTP_NOEXCEPT = 0;

        virtual int ssl_read_ (void *ssl, void *buff, int size) MANAPIHTTP_NOEXCEPT = 0;

        virtual bool ssl_early_data_is_enabled_ (void *ctx) MANAPIHTTP_NOEXCEPT = 0;

        virtual int ssl_write_early_data_ (void *ssl, const void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXCEPT = 0;

        virtual int ssl_read_early_data_ (void *ssl, void *buf, std::size_t num, std::size_t *readbytes) MANAPIHTTP_NOEXCEPT = 0;
        /**
         * 
         * @param ssl SSL connection
         * @return OK: 1; ERR: not 1
         */

        virtual int ssl_shutdown_ (void *ssl) MANAPIHTTP_NOEXCEPT = 0;

        /**
         * 
         * @param ssl SSL connection
         * @param flags sent/recv
         */

        virtual void ssl_set_shutdown_(void *ssl, int flags) MANAPIHTTP_NOEXCEPT = 0;

        virtual void ssl_free_(void *ssl) MANAPIHTTP_NOEXCEPT = 0;

        virtual int ssl_bio_write_ (void *rbio, const void *buff, int size) MANAPIHTTP_NOEXCEPT = 0;

        virtual int ssl_bio_read_ (void *wbio, void *buff, int size) MANAPIHTTP_NOEXCEPT = 0;

        virtual int ssl_bio_should_retry_ (void *bio) MANAPIHTTP_NOEXCEPT = 0;

        virtual int recv_setup_connection(const shared_conn &conn, tls_connection_t *storage) = 0;

        void update_limit_rate_connection(const shared_conn &sconn) MANAPIHTTP_NOEXCEPT override;

        static void connection_interface_eraser(worker::connection *data) MANAPIHTTP_NOEXCEPT;

        void shutdown_async_ (shared_conn conn);

        void onrecv(const std::shared_ptr<ev::tcp> &watcher, const shared_conn &conn, ibuffpool_t buffer) MANAPIHTTP_NOEXCEPT override;

        void *ctx = nullptr;

        int ssl_session_ctx_id{1};
    protected:
        int onaccept_event_(const worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT override;

        int conn_after_write(const worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT override;
    private:
        static shared_conn connection_init_cb (void *user_data) MANAPIHTTP_NOEXCEPT;

        int check_read_stack_full_ (const shared_conn &conn, tls_connection_t *data);

        int manapi_do_process (const shared_conn &conn, tls_connection_t *data);

        int manapi_do_handshake_ (const shared_conn &conn, tls_connection_t *data);

        int ssl_bio_flush_write_ (const shared_conn &conn, tls_connection_t *m, std::size_t max_cnt, bool fin);

        int ssl_bio_flush_read_ (const shared_conn &conn, tls_connection_t *m, std::size_t max_cnt);

        int ssl_flush_recv (const shared_conn &conn, connection_io_part *top, uint32_t *cnt);
    };
}