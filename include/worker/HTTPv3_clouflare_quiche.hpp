#pragma once

#include "UDP.hpp"
#include "http/HTTPv2.hpp"

#if MANAPIHTTP_QUICHE_DEPENDENCY

#include <quiche.h>

namespace manapi::net::worker {
    class http_v3_cloudflare_quiche : public udp {
    public:
        struct connection_stream_t {
            int64_t stream_id;
            std::shared_ptr<worker::connection> connection;
            std::atomic<int> status;
            std::function<void()> handle_io;

            int rbuff_pos;
            int wbuff_pos;
            int rbuff_caret;
            int wbuff_caret;

            std::weak_ptr<http::http_v2> client;
            bool finished;
            quiche_h3_header *headers;
            size_t headers_size;
            size_t header_cursor;

            uint8_t rbuff[65000];
            uint8_t wbuff[65000];
        };

        struct connection_t {
            std::string cid;
            quiche_conn *conn;
            // ev::timer timer;
            ev::timer quiche_timer;
            ev::async write_watcher;
            quiche_h3_conn *http3_conn;
            std::map <int64_t, std::shared_ptr<worker::connection>> streams;
            std::shared_ptr<worker::base> worker;
            std::atomic<int> status;
            size_t write_total;
            size_t read_total;
            size_t write_total_prev;
            size_t read_total_prev;
            std::atomic<size_t> stream_read_cnt;
            std::atomic<size_t> stream_write_cnt;
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

        explicit http_v3_cloudflare_quiche(net::site &site);
        ~http_v3_cloudflare_quiche() override;
        void onrecv(ev::io &watcher, int revents) override;
        void init() override;
        static std::shared_ptr<http_v3_cloudflare_quiche> create(net::site &site, std::shared_ptr<manapi::net::http::config> config);
        future<ssize_t> response(worker::connection &connection, http_response &resp, bool finish) override;
    private:
        static http_v3_cloudflare_quiche *_get_dynamic_worker (const std::shared_ptr<worker::base> &w);
        static bool _flush_write_stream (connection_t &conn_data, std::map<int64_t, std::shared_ptr<worker::connection>>::iterator &stream_it);
        static void _flush_write (connection_t &conn_data);
        static void _flush_read (connection_t &conn_data);
        static void _quiche_set_header (quiche_h3_header &header, std::string_view key, std::string_view value);
        static void _write_watcher_cb (EV_P_ ev_async *w, int revents);
        static void _flush_connection_closed (connection_t &conn_data);
        static void _clean_connection (connection_t *conn_data);
        void _stream_close (connection_stream_t &stream);
        void _reset_all_streams (connection_t &conn_data);
        static void _connection_timer_check (EV_P_ ev_timer *w, int revents);
        static void _quiche_timeout (EV_P_ ev_timer *w, int revents);
        static int _grab_headers (uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp);
        static bool _validate_token (std::string_view token, std::string &odcid, const sockaddr_storage &sockaddr_src, const socklen_t &sockaddr_len);
        static std::string _gen_mint_token (std::string_view dcid, const sockaddr_storage &sockaddr_src, const socklen_t &sockaddr_len);
        void _quiche_flush_egress(connection_t &connection);
        static void _quiche_timeout_again(connection_t &connection);
        std::string gbuffer{};
        std::map <std::string, std::shared_ptr<worker::connection>> connections;
        std::function<std::shared_ptr<manapi::net::worker::http_v3_cloudflare_quiche>()> new_dependency;

        static future<ssize_t> default_write (connection &conn, const void *buf, size_t size, bool flag);
        static future<ssize_t> default_read (connection &conn, void *buf, size_t size);

        quiche_config *_quiche_config{nullptr};
        quiche_h3_config *_quiche_h3_config{nullptr};
    };
}

#endif