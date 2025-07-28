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

        void close_connection(shared_conn conn, int flags) MANAPIHTTP_NOEXPECT override;

        int event_flags(const shared_conn &conn) MANAPIHTTP_NOEXPECT override;

        int event_flags(const shared_conn &conn, int flags) MANAPIHTTP_NOEXPECT override;

        std::unique_ptr<worker_watcher_cb> event_on(const shared_conn &conn, std::unique_ptr<worker_watcher_cb> callback) MANAPIHTTP_NOEXPECT override;

        void feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p)  MANAPIHTTP_NOEXPECT override;

        bool is_writable(const shared_conn &conn) MANAPIHTTP_NOEXPECT override;

        MANAPIHTTP_NODISCARD std::size_t recv_count(const shared_conn &conn) const MANAPIHTTP_NOEXPECT override;

        bytebuffer recv_first_buffer(const shared_conn &conn) MANAPIHTTP_NOEXPECT override;

        ssize_t sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXPECT override;

        ssize_t sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) MANAPIHTTP_NOEXPECT override;

        void waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXPECT override;

        std::string_view alpn_ossltest () MANAPIHTTP_NOEXPECT;

        void alpn_ossltest (std::string test) MANAPIHTTP_NOEXPECT;

        void close_stream(shared_conn s, int flags) MANAPIHTTP_NOEXPECT;

        void rst_stream (shared_conn s) MANAPIHTTP_NOEXPECT;

        connection::ipdata_t *ipdata(worker::connection *conn) MANAPIHTTP_NOEXPECT override;

        error::status_or<shared_conn> new_stream(shared_conn conn, base::stream_flags flags) MANAPIHTTP_NOEXPECT override;

        std::size_t stream_id(shared_conn s) MANAPIHTTP_NOEXPECT override;
    protected:
        void remove_poll_id (std::size_t poll_id) MANAPIHTTP_NOEXPECT;

        void flush_read_ (const shared_conn &conn, quic_stream_t *data) MANAPIHTTP_NOEXPECT;

        void flush_write_ (const shared_conn &conn, quic_stream_t *data) MANAPIHTTP_NOEXPECT;

        void update_limit_rate () MANAPIHTTP_NOEXPECT;

        virtual void update_limit_rate_connection (const shared_conn &sconn) MANAPIHTTP_NOEXPECT;

        static void stream_interface_eraser (worker::connection *n) MANAPIHTTP_NOEXPECT;

        static void connection_interface_eraser (worker::connection *n) MANAPIHTTP_NOEXPECT;

        static void timeout_event_cb (uv_timer_t *s) MANAPIHTTP_NOEXPECT;

        static void io_event_cb (uv_poll_t *s, int status, int events) MANAPIHTTP_NOEXPECT;

        static void io_unbind_cb (ev::handle *s) MANAPIHTTP_NOEXPECT;

        manapi::error::status_or<shared_conn> stream_accept (const shared_conn &conn, SSL *stream) MANAPIHTTP_NOEXPECT;

        void stream_processing (const shared_conn &s) MANAPIHTTP_NOEXPECT;

        void conn_processing (SSL *client) MANAPIHTTP_NOEXPECT;

        int try_init_conn_ (const shared_conn &conn) MANAPIHTTP_NOEXPECT;

        std::function<void()> finish;
        std::map<uintptr_t, shared_conn> conns_;
        int sock;
        std::size_t count;
        int flags;
    private:
        static manapi::error::status load_params (manapi::net::worker::openssl_quic *w, SSL_CTX *ctx, manapi::json sslconfig);

        static int select_alpn (SSL *ssl, const unsigned char **out, unsigned char *out_len, const unsigned char *in, unsigned int in_len, void *arg);

        std::vector<SSL_POLL_ITEM> polls_;
        std::unique_ptr<ev::io> w_;
        std::unique_ptr<ev::timer> t_;
        std::string alpn_ossltest_;
        void *ctx;
        void *listener;
    };
}
#   endif
#endif