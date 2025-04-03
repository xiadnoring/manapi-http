#pragma once

#include "../ManapiUtils.hpp"

#if MANAPIHTTP_WOLFSSL_DEPENDENCY

#include <netdb.h>

#include "../ManapiUtils.hpp"
#include "./base_worker.hpp"
#include "./TCP.hpp"

struct WOLFSSL;
struct WOLFSSL_CTX;

namespace manapi::net::worker {
    class WolfSSL_TLS : public worker::TCP {
    public:
        struct connection_interface : TCP::connection_interface {
            WOLFSSL *ssl{};
            std::unique_ptr<async::mutex> mx;
            manapi::timer accept_timer;
        };

        WolfSSL_TLS (net::site &site);
        ~WolfSSL_TLS ();
        bool is_valid_connection(worker::connection &connection) override;
        void init ();
        future<bool> configure_connection(std::shared_ptr<connection> conn) override;
        WolfSSL_TLS &operator=(WolfSSL_TLS &&n) noexcept;
        void disable_watcher_for_status(connection &conn, const connection_status &status) override;
        static std::shared_ptr<worker::WolfSSL_TLS> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);
        std::optional<std::shared_ptr<manapi::net::worker::connection>> accept () override;
        future<void> connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) override;
        int status (connection &conn) override;
    protected:
        void _recv_setup_connection(manapi::net::worker::connection &storage) override;
        void update_limit_rate_connection(connection &conn) override;
    private:
        static int _gl_wolfssl_async_callback (WOLFSSL *ssl, void *argp);
        int wolfssl_async_callback (connection &storage);

        static void connection_interface_eraser (void *ptr);
        WOLFSSL_CTX* ssl_create_context (const size_t &version = http::versions::TLS_v1_3);
        void ssl_configure_context ();

        future<ssize_t> ssl_write (connection &conn, const void *buff, ssize_t size);
        future<ssize_t> ssl_read (connection &conn, void *buff, ssize_t size);

        WOLFSSL_CTX *ctx = nullptr;
        int ssl_session_ctx_id{1};
    };
}
#endif