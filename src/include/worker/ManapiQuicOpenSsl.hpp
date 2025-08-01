#pragma once

#include "ManapiUtils.hpp"
#include "worker/ManapiUdp.hpp"

#if MANAPIHTTP_OPENSSL_DEPENDENCY

#   include <openssl/ssl.h>
#   include <openssl/quic.h>

#   if OPENSSL_VERSION_NUMBER >= ((3<<28)|(2<<20)|(0<<4)|0x0L)

#       define MANAPIHTTP_OPENSSL_QUIC_SUPPORT

namespace manapi::net::worker {
    class openssl_quic : public interface_worker {
    protected:
        struct quic_conn_t;

        struct quic_stream_t;
    public:
        openssl_quic (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, manapi::net::http::config *config);

        ~openssl_quic() override;

        static std::shared_ptr<worker::openssl_quic> create (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config);

        manapi::future<error::status> init(std::size_t deep) override;

        void stop(std::function<void()> cb) override;

        void close_connection(shared_conn conn, int flags) MANAPIHTTP_NOEXCEPT override;

        int event_flags(const shared_conn &conn) MANAPIHTTP_NOEXCEPT override;

        int event_flags(const shared_conn &conn, int flags) MANAPIHTTP_NOEXCEPT override;

        std::unique_ptr<worker_watcher_cb> event_on(const shared_conn &conn, std::unique_ptr<worker_watcher_cb> callback) MANAPIHTTP_NOEXCEPT override;

        void feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p)  MANAPIHTTP_NOEXCEPT override;

        bool is_writable(const shared_conn &conn) MANAPIHTTP_NOEXCEPT override;

        MANAPIHTTP_NODISCARD std::size_t recv_count(const shared_conn &conn) const MANAPIHTTP_NOEXCEPT override;

        bytebuffer recv_first_buffer(const shared_conn &conn) MANAPIHTTP_NOEXCEPT override;

        ssize_t sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT override;

        ssize_t sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) MANAPIHTTP_NOEXCEPT override;

        void waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXCEPT override;

        std::string_view alpn_ossltest () MANAPIHTTP_NOEXCEPT;

        void alpn_ossltest (std::string test) MANAPIHTTP_NOEXCEPT;

        void close_stream(shared_conn s, int flags) MANAPIHTTP_NOEXCEPT;

        void rst_stream (shared_conn s) MANAPIHTTP_NOEXCEPT;

        connection::ipdata_t *ipdata(worker::connection *conn) MANAPIHTTP_NOEXCEPT override;

        error::status_or<shared_conn> new_stream(const shared_conn &conn, int flags) MANAPIHTTP_NOEXCEPT override;

        std::size_t stream_id(const shared_conn &s) MANAPIHTTP_NOEXCEPT override;

        shared_conn stream_id(const shared_conn &conn, std::size_t id) MANAPIHTTP_NOEXCEPT override;

        MANAPIHTTP_NODISCARD std::size_t streams_size(const shared_conn &conn) const MANAPIHTTP_NOEXCEPT override;
    protected:
        void remove_poll_id (std::size_t poll_id) MANAPIHTTP_NOEXCEPT;

        void flush_read_ (const shared_conn &conn, quic_stream_t *data) MANAPIHTTP_NOEXCEPT;

        void flush_write_ (const shared_conn &conn, quic_stream_t *data) MANAPIHTTP_NOEXCEPT;

        void update_limit_rate () MANAPIHTTP_NOEXCEPT;

        virtual void update_limit_rate_connection (const shared_conn &sconn) MANAPIHTTP_NOEXCEPT;

        static void stream_interface_eraser (worker::connection *n) MANAPIHTTP_NOEXCEPT;

        static void connection_interface_eraser (worker::connection *n) MANAPIHTTP_NOEXCEPT;

        static void timeout_event_cb (uv_timer_t *s) MANAPIHTTP_NOEXCEPT;

        static void io_event_cb (uv_poll_t *s, int status, int events) MANAPIHTTP_NOEXCEPT;

        static void io_unbind_cb (ev::handle *s) MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<shared_conn> stream_accept (const shared_conn &conn, SSL *stream) MANAPIHTTP_NOEXCEPT;

        void stream_processing (const shared_conn &s) MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<shared_conn> conn_accept (SSL *client) MANAPIHTTP_NOEXCEPT;

        void conn_processing (SSL *client) MANAPIHTTP_NOEXCEPT;

        int try_init_conn_ (const shared_conn &conn) MANAPIHTTP_NOEXCEPT;

        std::function<void()> finish;
        std::map<uintptr_t, shared_conn> conns_;
        int sock;
        std::size_t count;
    private:
        static manapi::error::status load_params (manapi::net::worker::openssl_quic *w, SSL_CTX *ctx, manapi::json sslconfig);

        static int select_alpn (SSL *ssl, const unsigned char **out, unsigned char *out_len, const unsigned char *in, unsigned int in_len, void *arg);

        std::vector<SSL_POLL_ITEM> polls_;
        std::unique_ptr<ev::io> w_;
        manapi::timer update_limit_timer;
        std::unique_ptr<ev::timer> t_;
        std::string alpn_ossltest_;
        void *ctx;
        void *listener;
    };
}
#   endif
#endif