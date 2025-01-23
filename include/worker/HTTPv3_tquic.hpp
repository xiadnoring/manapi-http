#pragma once

#include "UDP.hpp"

#if MANAPIHTTP_TQUIC_DEPENDENCY

#include <tquic.h>

namespace manapi::net::worker {
    class http_v3_tquic : public udp {
    public:

        struct connection_t {
            quic_conn_t *conn;
            ev::timer timer;
            ev::async write_watcher;
            http3_conn_t *http3_conn;
            std::map <uint64_t, std::shared_ptr<worker::connection>> streams;
            http_v3_tquic *worker;
            std::atomic<int> status;
            size_t write_total;
            size_t read_total;
            size_t write_total_prev;
            size_t read_total_prev;
            std::atomic<size_t> stream_read_cnt;
            std::atomic<size_t> stream_write_cnt;
        };

        struct connection_stream_t {
            uint64_t stream_id;
            std::shared_ptr<worker::connection> connection;
            std::atomic<int> status;
            std::function<void()> handle_io;

            int rbuff_pos;
            int wbuff_pos;
            int rbuff_caret;
            int wbuff_caret;

            bool finished;
            http3_header_t *headers;
            size_t headers_size;

            uint8_t rbuff[65000];
            uint8_t wbuff[65000];
        };

        struct connection_io_await {
            std::function<void()> &iohandle;
            std::atomic<int> &iostatus;
            int status{0};
            ev::async *async_watcher{nullptr};
            std::atomic<size_t> *cnt{nullptr};

            void await_resume () noexcept {}
            bool await_ready () noexcept { return this->iostatus & CONN_CLOSED; }
            template<typename T>
            requires(std::is_base_of_v<promise_base, T>)
            void await_suspend (std::coroutine_handle<T> handle) {
                if (this->iostatus & CONN_CLOSED) {
                    future<>::resume_promise(handle);
                }
                else {
                    if (this->cnt) { this->cnt->fetch_add(1); }
                    this->iohandle = [cnt = this->cnt, handle = std::exchange(handle, nullptr)]() -> void {
                        if (cnt) { cnt->fetch_sub(1); }
                        future<>::resume_promise(handle);
                    };
                    this->iostatus.fetch_or(this->status);

                    if (this->async_watcher) {
                        this->async_watcher->send();
                    }
                }
            }
        };

        explicit http_v3_tquic(net::site &site);
        ~http_v3_tquic() override;
        void onrecv(ev::io &watcher, int revents) override;
        void init() override;
        static std::shared_ptr<http_v3_tquic> create(net::site &site, std::shared_ptr<manapi::net::http::config> config);
        future<ssize_t> response(worker::connection &connection, http_response &resp, bool finish) override;
    private:
        static http3_methods_t http3_methods;

        static void tquic_on_conn_closed (void *tctx, quic_conn_t *conn);
        static void tquic_on_conn_created (void *tctx, quic_conn_t *conn);
        static void tquic_on_conn_established (void *tctx, quic_conn_t *conn);
        static void tquic_on_new_token (void *tctx, quic_conn_t *conn, const uint8_t *token, size_t token_len);
        static void tquic_on_stream_closed (void *tctx, quic_conn_t *conn, uint64_t stream_id);
        static void tquic_on_stream_created (void *tctx, quic_conn_t *conn, uint64_t stream_id);
        static void tquic_on_stream_writable (void *tctx, quic_conn_t *conn, uint64_t stream_id);
        static void tquic_on_stream_readable (void *tctx, quic_conn_t *conn, uint64_t stream_id);
        static int tquic_on_packets_send (void *psctx, quic_packet_out_spec_t *pkts, unsigned int count);

        static void tquic_http3_on_conn_goaway (void *ctx, uint64_t stream_id);
        static void tquic_http3_on_stream_data (void *ctx, uint64_t stream_id);
        static void tquic_http3_on_stream_finished (void *ctx, uint64_t stream_id);
        static void tquic_http3_on_stream_headers (void *ctx, uint64_t stream_id, const struct http3_headers_t *headers, bool fin);
        static void tquic_http3_on_stream_priority_update (void *ctx, uint64_t stream_id);
        static void tquic_http3_on_stream_reset (void *ctx, uint64_t stream_id, uint64_t error_code);

        static bool _flush_write_stream (connection_t &conn_data, std::map<uint64_t, std::shared_ptr<connection>>::iterator &stream_it);
        static void _quic_set_header (http3_header_t &header, const std::string &key, const std::string &value);
        static void _write_watcher_cb (EV_P_ ev_async *w, int revents);
        static void _flush_connection_closed (connection_t &conn_data);
        void _stream_close (connection_stream_t &stream);
        void _reset_all_streams (connection_t &conn_data);
        static void _connection_timer_check (EV_P_ ev_timer *w, int revents);
        void _quic_timeout (ev::timer &timer, int revents);
        void _quic_timeout_again ();
        void _quic_try_new_connection (quic_conn_t *conn);
        static int _grab_headers (const uint8_t *name, size_t name_len, const uint8_t *value, size_t value_len, void *argp);
        static bool _validate_token (std::string_view token, std::string &odcid, const sockaddr_storage &sockaddr_src, const socklen_t &sockaddr_len);
        static std::string _gen_mint_token (std::string_view dcid, const sockaddr_storage &sockaddr_src, const socklen_t &sockaddr_len);
        void _quic_process_connections ();

        std::string gbuffer{};
        std::function<std::shared_ptr<manapi::net::worker::http_v3_tquic>()> new_dependency;

        static future<ssize_t> default_write (connection &conn, const void *buf, size_t size, bool flag);
        static future<ssize_t> default_read (connection &conn, void *buf, size_t size);

        std::string application_h3_proto = "h3";
        quic_config_t *_quic_config{nullptr};
        quic_tls_config_t *_quic_tls_config{nullptr};
        http3_config_t *_quic_h3_config{nullptr};
        quic_endpoint_t *_quic_server{nullptr};

        quic_transport_methods_t handler_methods{};
        quic_packet_send_methods_t sender_methods{};
        std::shared_ptr<ev::timer> timeout{nullptr};
        std::map <uint64_t, quic_conn_t *> connections;
        //std::unique_ptr<ev::idle> process_connections;
    };
}

#endif