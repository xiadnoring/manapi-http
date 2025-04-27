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
        };

        TLS (net::site &site);
        ~TLS () override;
        bool is_valid_connection(worker::connection &connection) override;
        void init () override;
        future<bool> configure_connection(std::shared_ptr<connection> conn) override;
        std::optional<std::shared_ptr<manapi::net::worker::connection>> accept () override;
        void connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) override;
        ssize_t sync_read(worker::connection *conn, void *buff, ssize_t size) override;
        ssize_t sync_write(worker::connection *conn, const void *buff, ssize_t size) override;
        manapi::future<std::shared_ptr<ev::io>> async_watch_io(worker::connection *conn, int revents, ev::io_cb callback) override;
        std::shared_ptr<ev::io> sync_watch_io(worker::connection *conn, int revents, ev::io_cb callback) override;
    protected:
        int  ssl_error_none_, ssl_error_want_read_,
        ssl_error_want_write_, ssl_error_zero_return_,
        ssl_error_ssl_, ssl_error_syscall_,
        ssl_send_shutdown_, ssl_recv_shutdown_;

        virtual bool ssl_is_init_fininshed_ (void *ssl);
        virtual int ssl_get_error_ (void *ssl, int rhs);
        virtual int ssl_accept_ (void *ssl);
        virtual void *ssl_new_ (void *ctx);
        virtual int ssl_write_ (void *ssl, const void *buff, int size);
        virtual int ssl_read_ (void *ssl, void *buff, int size);
        virtual int ssl_shutdown_ (void *ssl);
        virtual void ssl_set_shutdown_(void *ssl, int flags);
        virtual void ssl_free_(void *ssl);

        void recv_setup_connection(manapi::net::worker::connection &storage) override;
        void update_limit_rate_connection(connection &conn) override;
        static void connection_interface_eraser(void *data);
        virtual void* ssl_create_context (const size_t &version);
        virtual void ssl_configure_context ();

        future<ssize_t> ssl_write (connection &conn, const void *buff, ssize_t size);
        future<ssize_t> ssl_read (connection &conn, void *buff, ssize_t size);

        void *ctx = nullptr;
        int ssl_session_ctx_id{1};
    };
}