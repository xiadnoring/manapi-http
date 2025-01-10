#pragma once

#if MANAPIHTTP_OPENSSL_DEPENDENCY
#include <openssl/ssl.h>

#include <netdb.h>

#include "./Base.hpp"
#include "./TCP.hpp"

namespace manapi::net::worker {
    class OpenSSL_TLS : public worker::TCP {
    public:
        struct connection_interface : TCP::connection_interface {
            SSL *ssl{};
            std::unique_ptr<async::mutex> mx;
            std::atomic<int> timer_accept = 0;
        };

        OpenSSL_TLS (net::site &site);
        ~OpenSSL_TLS ();
        bool is_valid_connection(worker::connection &connection) override;
        void init ();
        future<bool> configure_connection(std::shared_ptr<connection> conn) override;
        OpenSSL_TLS &operator=(OpenSSL_TLS &&n) noexcept;
        void disable_watcher_for_status(connection &conn, const connection_status &status) override;
        static std::shared_ptr<worker::OpenSSL_TLS> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);
        std::optional<std::shared_ptr<manapi::net::worker::connection>> accept () override;
        future<void> connection_close(std::shared_ptr<connection> conn) override;
    protected:
        void _recv_setup_connection(manapi::net::worker::connection &storage) override;
    private:
        void _lookup_event(ev::io &watcher, std::shared_ptr<connection> storage, const int &revents) override;

        static void connection_interface_eraser (void *ptr);
        static int _gl_openssl_async_callback (SSL *ssl, void *argp);
        int openssl_async_callback (connection &storage);
        SSL_CTX* ssl_create_context (const size_t &version = http::versions::TLS_v1_3);
        void ssl_configure_context ();
        void ssl_get_error ();

        future<ssize_t> ssl_write (connection &conn, const void *buff, const size_t &size);
        future<ssize_t> ssl_read (connection &conn, void *buff, const size_t &size);

        SSL_CTX *ctx = nullptr;
    };
}
#endif