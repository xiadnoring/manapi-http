#pragma once

#include "./UDP.hpp"
#include "../ManapiUtils.hpp"
#include "../http/HTTPv2.hpp"

#if MANAPIHTTP_QUICHE_DEPENDENCY

struct quiche_conn;
struct quiche_h3_conn;
struct quiche_config;
struct quiche_h3_config;

namespace manapi::net::worker {
    class http_v3_cloudflare_quiche : public udp {
    public:

        struct connection_t {
            std::string cid;
            int flags;
            http_v3_cloudflare_quiche *worker;
            quiche_conn *conn;
            quiche_h3_conn *http3_conn;
            std::unique_ptr<std::map <int64_t, std::shared_ptr<worker::connection>>> streams;
            manapi::timer timeout;
            shared_conn self;
        };

        struct connection_stream_t : worker::base::connection_base_t {
            int flags;
            int64_t id;
            connection_t *conn;
            std::unique_ptr<http::request_data_t> req;
            std::unique_ptr<struct connection_io> top;
            std::unique_ptr<worker_watcher_cb> ev_callback;
            int speed_min_delay;
        };

        explicit http_v3_cloudflare_quiche(net::http::site site, std::shared_ptr<worker::worker_config_t> wdata,manapi::net::http::config * config);

        ~http_v3_cloudflare_quiche() override;

        static std::shared_ptr<worker::http_v3_cloudflare_quiche> create (net::http::site site, std::shared_ptr<worker::worker_config_t> wdata, std::shared_ptr<manapi::net::http::config> config);

        void init() override;

        void stop(std::function<void()> cb) override;

        void onrecv(std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) override;

        ssize_t sync_write(const shared_conn &conn, const void *buff, ssize_t size, bool finish) override;

        ssize_t sync_write_ex(const shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt) override;

        void close_connection(shared_conn conn, bool clean_disconnect) override;

        void configure_connection(const shared_conn &conn, oncont_cb cb) override;

        int event_flags(const shared_conn &conn) override;

        int event_flags(const shared_conn &conn, int flags) override;

        std::unique_ptr<worker_watcher_cb> event_on(const shared_conn &conn, std::unique_ptr<worker_watcher_cb> callback) override;

        void feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) override;

        bool is_valid_connection(worker::connection *connection) override;

        bool is_writable(const shared_conn &conn) override;

        future<ssize_t> response(const shared_conn &connection, http::response *resp, bool finish) override;
    protected:
        int flags;

        int count;

        std::function<void()> finish;

        // void recv_buffer_alloc_(ssize_t nread, ev::buff_t *buff) override;
        //
        // void recv_buffer_dealloc_(const ev::buff_t *buf) override;

    private:
        void update_limit_rate ();

        virtual void update_limit_rate_connection (const shared_conn &conn);

        virtual void update_limit_rate_stream (const shared_conn &conn);

        static void flush_write_ (const shared_conn &conn, connection_t *conn_data);

        int flush_read_buffers_ (const shared_conn &conn, connection_stream_t *s, connection_io_part *top, int *cnt);

        int flush_read_ (const shared_conn &stream);

        static void force_close_ (shared_conn conn, connection_t *conn_data);

        static void flush_connection_closed_ (const shared_conn &conn, connection_t *conn_data);

        void reset_all_streams_ (connection_t *conn_data);

        connection::ipdata_t *ipdata(worker::connection *conn) override;

        static void quiche_timeout_ (manapi::timer t, const shared_conn &connection);

        static int grab_headers_ (uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp);

        static bool validate_token_ (char *token, size_t token_len, char *odcid, size_t *odcid_len, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len);

        static int gen_mint_token_ (char *dcid, size_t dcid_len, char *token, size_t *token_len, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len);

        int quiche_flush_egress_(const shared_conn &connection, connection_t *data);

        static void quiche_timeout_again_(connection_t *connection);

        std::weak_ptr<http_v3_cloudflare_quiche> self_;
        std::map <std::string, shared_conn> connections;
        quiche_config *quiche_config_{nullptr};
        quiche_h3_config *quiche_h3_config_{nullptr};
        manapi::timer limit_rate_timer{};
    };
}

#endif