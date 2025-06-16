#pragma once

#include "../ManapiUtils.hpp"

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

#include "./base_worker.hpp"
#include "./TCP.hpp"

namespace manapi::net::worker {
    class TLS : public worker::TCP {
    public:
        struct connection_interface : TCP::connection_interface {
            void *ssl;
            manapi::timer accept_timer;
            void *rbio;
            void *wbio;
        };

        TLS (net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, manapi::net::http::config *config);

        ~TLS () override;

        void init () override;
        
        void configure_connection(const shared_conn &connection, oncont_cb cb) override;

        shared_conn accept (ev::shared_tcp &w) override;

        void close_connection(shared_conn conn, bool clean_disconnect) override;

        ssize_t sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) override;

        ssize_t sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) override;

        int event_flags(const shared_conn & conn, int flags) noexcept(true) override;

    protected:
        int ssl_error_none_, ssl_error_want_read_,
        ssl_error_want_write_, ssl_error_zero_return_,
        ssl_error_ssl_, ssl_error_syscall_,
        ssl_send_shutdown_, ssl_recv_shutdown_;

        virtual bool ssl_is_init_fininshed_ (void *ssl) = 0;

        virtual int ssl_get_error_ (void *ssl, int rhs) = 0;

        virtual int ssl_accept_ (void *ssl) = 0;

        virtual void *ssl_new_ (void *ctx) = 0;

        virtual int ssl_write_ (void *ssl, const void *buff, int size) = 0;

        virtual int ssl_read_ (void *ssl, void *buff, int size) = 0;

        virtual int ssl_shutdown_ (void *ssl) = 0;

        virtual void ssl_set_shutdown_(void *ssl, int flags) = 0;

        virtual void ssl_free_(void *ssl) = 0;

        virtual int ssl_bio_write_ (void *rbio, const void *buff, int size) = 0;

        virtual int ssl_bio_read_ (void *wbio, void *buff, int size) = 0;

        virtual int ssl_bio_should_retry_ (void *bio) = 0;

        virtual bool recv_setup_connection(connection_interface *storage) = 0;

        bool update_limit_rate_connection(const shared_conn &sconn) override;

        static void connection_interface_eraser(worker::connection *data);

        virtual void* ssl_create_context (const size_t &version) = 0;

        virtual void ssl_configure_context () = 0;

        void onrecv(std::shared_ptr<ev::tcp> &watcher, const shared_conn &conn, ibuffpool_t buffer) override;

        void *ctx = nullptr;
        int ssl_session_ctx_id{1};
    private:
        int check_read_stack_full_ (connection_interface *data);

        int ssl_bio_flush_write_ (const shared_conn &conn, TLS::connection_interface *m, int max_cnt);

        int ssl_bio_flush_read_ (const shared_conn &conn, void *rbio, connection_io_part *top, int *cnt, int max_cnt);

        int ssl_flush_recv (const shared_conn &conn, connection_io_part *top, int *cnt);
    };
}