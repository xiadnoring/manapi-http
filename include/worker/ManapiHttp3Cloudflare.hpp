#pragma once

#include "./ManapiUdp.hpp"
#include "../ManapiUtils.hpp"
#include "../http/ManapiBaseHttp.hpp"
#include "../worker/ManapiBaseWorker.hpp"
#include "../components/ManapiTimerObject.hpp"

#if MANAPIHTTP_QUICHE_DEPENDENCY

struct quiche_conn;
struct quiche_h3_conn;
struct quiche_config;
struct quiche_h3_config;

namespace manapi::net::worker {
    class http_v3_cloudflare_quiche : public udp {
    public:
        struct queue_udp_send_t {
            ev::udp_send *w;
            std::unique_ptr<queue_udp_send_t> next;
        };

        struct connection_t;

        struct connection_stream_t;

        explicit http_v3_cloudflare_quiche(net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata,manapi::net::http::config * config);

        ~http_v3_cloudflare_quiche() override;

        static std::shared_ptr<worker::http_v3_cloudflare_quiche> create (net::http::site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<manapi::net::http::config> config);

        manapi::future<manapi::error::status> init(std::size_t deep) override;

        void stop(std::function<void()> cb) override;

        void onrecv(const std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) MANAPIHTTP_NOEXCEPT override;

        ssize_t sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT override;

        ssize_t sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) MANAPIHTTP_NOEXCEPT override;

        void close_connection(shared_conn conn, int flags) MANAPIHTTP_NOEXCEPT override;

        int event_flags(const shared_conn &conn) MANAPIHTTP_NOEXCEPT override;

        int event_flags(const shared_conn &conn, int flags) MANAPIHTTP_NOEXCEPT override;

        worker_watcher_cb event_on(const shared_conn &conn, worker_watcher_cb callback) MANAPIHTTP_NOEXCEPT override;

        void feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT override;

        bool is_writable(const shared_conn &conn) MANAPIHTTP_NOEXCEPT override;

        MANAPIHTTP_NODISCARD std::size_t recv_count(const shared_conn &conn) const MANAPIHTTP_NOEXCEPT override;

        bytebuffer recv_first_buffer(const shared_conn &conn) MANAPIHTTP_NOEXCEPT override;
    protected:
        int flags;

        int count;

        std::function<void()> finish;

        // void recv_buffer_alloc_(ssize_t nread, ev::buff_t *buff) override;
        //
        // void recv_buffer_dealloc_(const ev::buff_t *buf) override;

    private:
        static manapi::future<int> cloudflare_wrk_http3_send_response (const manapi::net::worker::shared_conn &conn, manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w, manapi::net::http::response* res, bool finish);

        void update_limit_rate () MANAPIHTTP_NOEXCEPT;

        virtual void update_limit_rate_connection (const shared_conn &conn) MANAPIHTTP_NOEXCEPT;

        virtual void update_limit_rate_stream (const shared_conn &conn) MANAPIHTTP_NOEXCEPT;

        static void flush_write_ (const shared_conn &conn, connection_t *conn_data) MANAPIHTTP_NOEXCEPT;

        int flush_read_buffers_ (const shared_conn &conn, connection_stream_t *s) MANAPIHTTP_NOEXCEPT;

        int flush_read_ (const shared_conn &stream) MANAPIHTTP_NOEXCEPT;

        static void force_close_ (shared_conn conn, connection_t *conn_data) MANAPIHTTP_NOEXCEPT;

        void wrk_global(wrk_interface_global_t *data) MANAPIHTTP_NOEXCEPT override;

        static void flush_connection_closed_ (const shared_conn &conn, connection_t *conn_data) MANAPIHTTP_NOEXCEPT;

        void reset_all_streams_ (connection_t *conn_data) MANAPIHTTP_NOEXCEPT;

        connection::ipdata_t *ipdata(worker::connection *conn) MANAPIHTTP_NOEXCEPT override;

        static void quiche_timeout_ (manapi::timer t, const shared_conn &connection) MANAPIHTTP_NOEXCEPT;

        static int grab_headers_ (uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp) MANAPIHTTP_NOEXCEPT;

        static bool validate_token_ (char *token, size_t token_len, char *odcid, size_t *odcid_len, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len) MANAPIHTTP_NOEXCEPT;

        static int gen_mint_token_ (char *dcid, size_t dcid_len, char *token, size_t *token_len, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len) MANAPIHTTP_NOEXCEPT;

        int quiche_flush_egress_(connection_t *data) MANAPIHTTP_NOEXCEPT;

        void waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXCEPT override;

        static void quiche_timeout_again_(connection_t *connection) MANAPIHTTP_NOEXCEPT;

        static void connection_interface_eraser (worker::connection *ptr) MANAPIHTTP_NOEXCEPT;

        static void stream_interface_eraser (worker::connection *ptr) MANAPIHTTP_NOEXCEPT;

        std::map <std::string, shared_conn, std::less<>> connections;
        quiche_config *quiche_config_{nullptr};
        quiche_h3_config *quiche_h3_config_{nullptr};
        manapi::timer limit_rate_timer{};
    };
}

#endif