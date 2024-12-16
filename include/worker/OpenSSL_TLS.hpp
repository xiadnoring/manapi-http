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
        };

        OpenSSL_TLS (net::site &site);
        OpenSSL_TLS (OpenSSL_TLS && n) noexcept;
        ~OpenSSL_TLS ();
        bool is_valid_connection(worker::connection &connection) override;
        void init ();
        bool configure_connection (worker::connection &connection) const override;
        OpenSSL_TLS &operator=(OpenSSL_TLS &&n) noexcept;
        void onrecv(const std::shared_ptr<worker::base> &worker) override;
        static std::shared_ptr<worker::OpenSSL_TLS> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);

        std::pair <bool, std::shared_ptr<manapi::net::worker::connection>> accept () override;
    private:
        static Atomic <bool> gl_init;
        static void connection_interface_eraser (void *ptr);
        static SSL_CTX* ssl_create_context (const size_t &version = http::versions::TLS_v1_3);
        void ssl_configure_context ();
        std::string ssl_get_error ();

        bool established (worker::connection &conn, bool flag) const override;

        future<ssize_t> ssl_write (connection &conn, const void *buff, const size_t &size);
        future<ssize_t> ssl_read (connection &conn, void *buff, const size_t &size);

        std::mutex wmx, rmx, ssldbgmx;
        SSL_CTX *ctx = nullptr;
    };
}
#endif