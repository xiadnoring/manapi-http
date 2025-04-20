#pragma once

#include "./UDP.hpp"
#include "../ManapiUtils.hpp"
#include "../http/HTTPv2.hpp"

#if MANAPIHTTP_QUICHE_DEPENDENCY

#include <quiche.h>

namespace manapi::net::worker {
    class http_v3_cloudflare_quiche : public udp {
    public:
        struct buffer_stack_t {
            manapi::object_item_pool<bytebuffer, std::size_t> buffer;
            std::unique_ptr<buffer_stack_t> next;
        };

        enum stream_atomic_status {
            STREAM_ATOMIC_CONN_CLOSED = 0b1,
            STREAM_ATOMIC_CONN_HEADERS = 0b10,
            STREAM_ATOMIC_CONN_WRITE = 0b100,
            STREAM_ATOMIC_CONN_READ = 0b1000,
            STREAM_ATOMIC_CONN_RECV_END = 0b10000,
            STREAM_ATOMIC_CONN_SEND_END = 0b100000
        };

        enum stream_status {
            STREAM_CONN_RECV_END = 0b1,
            STREAM_CONN_SEND_END = 0b10,
            STREAM_CONN_IO = 0b100,
        };

        enum connection_flags {
            CONN_REVIEW_STREAMS = 0b1
        };

        struct connection_t {
            std::string cid;
            quiche_conn *conn;
            // ev::timer timer;
            ev::timer quiche_timer;
            manapi::timer io_timer;
            ev::async write_watcher;
            quiche_h3_conn *http3_conn;
            std::map <int64_t, std::shared_ptr<worker::connection>> streams;
            std::shared_ptr<worker::base> worker;
            int status;
            int flags;
            std::vector<uint64_t> closed_streams;
            size_t transfared_last_second;
        };

        struct connection_stream_t {
            int64_t stream_id;
            std::shared_ptr<worker::connection> connection;
            async::mutex mx;
            int status;
            int status2;
            std::atomic<int> atomic_status;

            std::unique_ptr<std::map<std::string, std::string>> headers;
            std::unique_ptr<quiche_h3_header> quiche_headers;
            int headers_size;
            int header_cursor;

            std::unique_ptr<buffer_stack_t> flush_write_buffer;

            buffer_stack_t *flush_write_last;
            int flush_write_total_size;

            int flush_write_cursor;
            int flush_write_current;

            int flush_read_cursor;

            manapi::object_item_pool<bytebuffer, std::size_t> write_buffer, read_buffer, write_buffer2, read_buffer2;
            int write_cursor, read_cursor;

            size_t transfared_last_second;
            int current_delay;
        };

        explicit http_v3_cloudflare_quiche(net::site &site);
        ~http_v3_cloudflare_quiche() override;
        void onrecv(std::shared_ptr<ev::io> &watcher, int status, int revents) override;
        void init() override;
        static std::shared_ptr<http_v3_cloudflare_quiche> create(net::site &site, std::shared_ptr<manapi::net::http::config> config);
        future<ssize_t> response(worker::connection &connection, http::response &resp, bool finish) override;
        void stop() override;

    private:
        void update_limit_rate ();
        virtual void update_limit_rate_connection (connection &conn);
        static http_v3_cloudflare_quiche *_get_dynamic_worker (const std::shared_ptr<worker::base> &w);
        static void _flush_write_stream (connection_t &conn_data, std::map<int64_t, std::shared_ptr<worker::connection>>::iterator &stream_it, bool app);
        static void _flush_read_stream (connection_t &conn_data, std::map<int64_t, std::shared_ptr<worker::connection>>::iterator &stream_it);
        static void _flush_write (connection_t &conn_data, bool app);
        static void _flush_read (connection_t &conn_data);
        static void _quiche_set_header (quiche_h3_header &header, std::string_view key, std::string_view value);
        static void _write_watcher_cb (EV_P_ ev_async *w, int revents);
        static void _force_close (connection_t &conn_data);
        static void _flush_connection_closed (connection_t &conn_data);
        static void _clean_connection (void *conn_data);
        static void init_write_buffer(connection_stream_t &s);
        static void disable_status_io (connection_stream_t &s);
        static void enable_status_io(connection_stream_t &s);
        ssize_t buffer_size();
        void _stream_close (connection_stream_t &stream);
        void _reset_all_streams (connection_t &conn_data);
        void _io_timeout (connection_t &conn_data);
        static void _quiche_timeout (EV_P_ ev_timer *w, int revents);
        static int _grab_headers (uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp);
        static bool _validate_token (std::string_view token, std::string &odcid, const sockaddr_storage &sockaddr_src, const socklen_t &sockaddr_len);
        static std::string _gen_mint_token (std::string_view dcid, const sockaddr_storage &sockaddr_src, const socklen_t &sockaddr_len);
        void _quiche_flush_egress(connection_t &connection);
        static void _quiche_timeout_again(connection_t &connection);
        std::string gbuffer{};
        size_t gbuffer_size{0};
        std::map <std::string, std::shared_ptr<worker::connection>> connections;
        std::function<std::shared_ptr<manapi::net::worker::http_v3_cloudflare_quiche>()> new_dependency;

        static future<ssize_t> default_write (connection &conn, const void *buf, ssize_t size, bool flag);
        static future<ssize_t> default_read (connection &conn, void *buf, ssize_t size);

        quiche_config *_quiche_config{nullptr};
        quiche_h3_config *_quiche_h3_config{nullptr};
        manapi::timer limit_rate_timer{};
    };
}

#endif