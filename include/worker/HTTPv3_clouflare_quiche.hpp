#pragma once

#include "./UDP.hpp"
#include "../ManapiUtils.hpp"
#include "../http/HTTPv2.hpp"

#if MANAPIHTTP_QUICHE_DEPENDENCY

#include <quiche.h>

// namespace manapi::net::worker {
//     class http_v3_cloudflare_quiche : public udp {
//     public:
//         enum stream_status {
//             STREAM_CONN_RECV_END = 0b1,
//             STREAM_CONN_SEND_END = 0b10,
//             STREAM_CONN_IO = 0b100,
//         };
//
//         enum connection_flags {
//             CONN_REVIEW_STREAMS = 0b1
//         };
//
//         struct connection_t {
//             std::string cid;
//             quiche_conn *conn;
//             quiche_h3_conn *http3_conn;
//             ev::shared_timer quiche_timer;
//             std::map <int64_t, std::shared_ptr<worker::connection>> streams;
//             std::shared_ptr<worker::base> worker;
//             size_t transfared_last_second;
//             int status;
//         };
//
//         struct connection_stream_t {
//             int64_t stream_id;
//             worker::connection *connection;
//             int status;
//             std::unique_ptr<std::map<std::string, std::string>> headers;
//             std::unique_ptr<quiche_h3_header> quiche_headers;
//             int headers_size;
//             int header_cursor;
//             size_t transfared_last_second;
//             int current_delay;
//         };
//
//         explicit http_v3_cloudflare_quiche(net::site &site);
//         ~http_v3_cloudflare_quiche() override;
//         void onrecv(std::shared_ptr<ev::udp> &watcher, char *buff, ssize_t size, const sockaddr *addr, unsigned flags) override;
//         void init() override;
//         static std::shared_ptr<http_v3_cloudflare_quiche> create(net::site &site, std::shared_ptr<manapi::net::http::config> config);
//         future<ssize_t> response(worker::connection *connection, http::response *resp, bool finish) override;
//         void stop() override;
//
//         void configure_connection(connection *conn, oncont_cb cb) override;
//         void close_connection(worker::connection *conn, bool clean_disconnect) override;
//         bool is_valid_connection(worker::connection *connection) override;
//         ssize_t sync_write(worker::connection *conn, const void *buff, ssize_t size, bool finish) override;
//     protected:
//         void recv_buffer_alloc_(ssize_t nread, ev::buff_t *buff) override;
//         void recv_buffer_dealloc_(const ev::buff_t *buf) override;
//     private:
//         void update_limit_rate ();
//         virtual void update_limit_rate_connection (connection &conn);
//         static http_v3_cloudflare_quiche *get_dynamic_worker_ (const std::shared_ptr<worker::base> &w);
//         static void flush_write_stream_ (connection_t *conn_data, std::map<int64_t, std::shared_ptr<worker::connection>>::iterator &stream_it, bool app);
//         static void flush_read_stream_ (connection_t *conn_data, std::map<int64_t, std::shared_ptr<worker::connection>>::iterator &stream_it);
//         static void flush_write_ (connection_t *conn_data, bool app);
//         static void flush_read_ (connection_t *conn_data);
//         static void quiche_set_header_ (quiche_h3_header &header, std::string_view key, std::string_view value);
//         static void force_close_ (connection_t *conn_data);
//         static void flush_connection_closed_ (connection_t *conn_data);
//         static void clean_connection_ (void *conn_data);
//         ssize_t buffer_size();
//         void _stream_close (connection_stream_t *stream);
//         void _reset_all_streams (connection_t *conn_data);
//         void _io_timeout (connection_t *conn_data);
//         static void quiche_timeout_ (std::shared_ptr<ev::timer> t, worker::connection *connection);
//         static int _grab_headers (uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp);
//         static bool _validate_token (std::string_view token, std::string &odcid, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len);
//         static std::string _gen_mint_token (std::string_view dcid, const sockaddr *sockaddr_src, const socklen_t &sockaddr_len);
//         void _quiche_flush_egress(connection_t *connection);
//         static void _quiche_timeout_again(connection_t *connection);
//
//         std::weak_ptr<http_v3_cloudflare_quiche> self_;
//         std::map <std::string, std::shared_ptr<worker::connection>> connections;
//         std::function<std::shared_ptr<manapi::net::worker::http_v3_cloudflare_quiche>()> new_dependency;
//
//         quiche_config *_quiche_config{nullptr};
//         quiche_h3_config *_quiche_h3_config{nullptr};
//         manapi::timer limit_rate_timer{};
//         char *recv_buffer;
//         bool recv_buffer_freed;
//     };
// }

#endif