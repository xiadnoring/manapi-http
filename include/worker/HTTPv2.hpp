#pragma once

#include <thread>

#include "../ManapiUtils.hpp"
#include "../ManapiSite.hpp"
#include "./base_worker.hpp"
#include "TCP.hpp"
#include "../compress/ManapiHPack.hpp"
#include "components/Buffer.hpp"

namespace manapi::net::worker {
    enum http2_frame_type {
        HTTP2_FRAME_DATA = 0x00,
        HTTP2_FRAME_HEADERS = 0x01,
        HTTP2_FRAME_PRIORITY = 0x02,
        HTTP2_FRAME_RST_STREAM = 0x03,
        HTTP2_FRAME_SETTINGS = 0x04,
        HTTP2_FRAME_PUSH_PROMISE = 0x05,
        HTTP2_FRAME_PING = 0x06,
        HTTP2_FRAME_GOAWAY = 0x07,
        HTTP2_FRAME_WINDOW_UPDATE = 0x08,
        HTTP2_FRAME_CONTINUATION = 0x09,
        HTTP2_FRAME_ALTSVC = 0x0a,
        HTTP2_FRAME_ORIGIN = 0x0c,
        HTTP2_FRAME_PRIORITY_UPDATE = 0x10
    };

    enum http2_flag_type {
        HTTP2_FLAG_HEADERS_END_STREAM     = 0b00000001,
        HTTP2_FLAG_HEADERS_END_HEADERS    = 0b00000100,
        HTTP2_FLAG_HEADERS_PADDED         = 0b00001000,
        HTTP2_FLAG_HEADERS_PRIORITY       = 0b00100000,

        HTTP2_FLAG_SETTINGS_ACK           = 0b00000001,

        HTTP2_FLAG_DATA_PADDED            = 0b00001000,
        HTTP2_FLAG_DATA_END_STREAM        = 0b00000001,

        HTTP2_FLAG_PING_ACK               = 0b00000001,
    };

    enum http2_setting_type {
        HTTP2_SETTING_RESERVED = 0x00,
        HTTP2_SETTING_HEADER_TABLE_SIZE = 0x01,
        HTTP2_SETTING_ENABLE_PUSH = 0x02,
        HTTP2_SETTING_MAX_CONCURRENT_STREAMS = 0x03,
        HTTP2_SETTING_INITIAL_WINDOW_SIZE = 0x04,
        HTTP2_SETTING_MAX_FRAME_SIZE = 0x05,
        HTTP2_SETTING_MAX_HEADER_LIST_SIZE = 0x06,
        HTTP2_SETTING_SETTINGS_ENABLE_CONNECT_PROTOCOL = 0x08,
        HTTP2_SETTING_SETTINGS_NO_RFC7540_PRIORITIES = 0x09,
        HTTP2_SETTING_TLS_RENEG_PERMITTED = 0x10,
        HTTP2_SETTING_SETTINGS_ENABLE_METADATA = 0x4d44
    };

    enum http2_error_type {
        HTTP2_ERROR_NO_ERROR = 0x00,              // Graceful shutdown
        HTTP2_ERROR_PROTOCOL_ERROR = 0x01,        // Protocol error detected
        HTTP2_ERROR_INTERNAL_ERROR = 0x02,        // Implementation fault
        HTTP2_ERROR_FLOW_CONTROL_ERROR = 0x03,    // Flow-control limits exceeded
        HTTP2_ERROR_SETTINGS_TIMEOUT = 0x04,      // Settings not acknowledged
        HTTP2_ERROR_STREAM_CLOSED = 0x05,         // Frame received for closed stream
        HTTP2_ERROR_FRAME_SIZE_ERROR = 0x06,      // Frame size incorrect
        HTTP2_ERROR_REFUSED_STREAM = 0x07,        // Stream not processed
        HTTP2_ERROR_CANCEL = 0x08,                // Stream cancelled
        HTTP2_ERROR_COMPRESSION_ERROR = 0x09,     // Compression state not updated
        HTTP2_ERROR_CONNECT_ERROR = 0x0a,         // TCP connection error for CONNECT method
        HTTP2_ERROR_ENHANCE_YOUR_CALM = 0x0b,     // Processing capacity exceeded
        HTTP2_ERROR_INADEQUATE_SECURITY = 0x0c,   // Negotiated TLS parameters not acceptable
        HTTP2_ERROR_HTTP_1_1_REQUIRED = 0x0d      // Use HTTP/1.1 for the request
    };

    enum http2_conn_type {
        HTTP2_CONN_IDLE = 0,
        HTTP2_CONN_RESERVED_LOCAL = 1,
        HTTP2_CONN_RESERVED_REMOTE = 2,
        HTTP2_CONN_OPEN = 3,
        HTTP2_CONN_HALF_CLOSED_LOCAL = 4,
        HTTP2_CONN_HALF_CLOSED_REMOTE = 5,
        HTTP2_CONN_CLOSED = 6
    };

    enum http2_thread_flags {
        HTTP2_THREAD_RECV_EOS   = 0b1,
        HTTP2_THREAD_SEND_EOS   = 0b10,
        HTTP2_THREAD_HAS_BODY   = 0b100,
        HTTP2_THREAD_EOS = 0b1000,
        HTTP2_THREAD_IO = 0b10000
    };

    enum http2_thread_atomic_flags {
        HTTP2_THREAD_ATOMIC_RST        = 0b1,
        HTTP2_THREAD_ATOMIC_RECV_EOS   = 0b10,
        HTTP2_THREAD_ATOMIC_SEND_EOS   = 0b100,
        HTTP2_THREAD_ATOMIC_WANT_READ  = 0b1000,
        HTTP2_THREAD_ATOMIC_WANT_WRITE = 0b10000,
        HTTP2_THREAD_ATOMIC_HEADERS    = 0b100000
    };

    class http_v2 : public worker::base {
        struct http_v2_write_buffers {
            manapi::object_item_pool<bytebuffer, std::size_t> buffer;
            std::unique_ptr<http_v2_write_buffers> next;
        };

        struct http_v2_settings_t {
            int header_table_size;
            int enable_push;
            int max_concurret_streams;
            int initial_window_size;
            int max_frame_size;
            int max_header_list_size;
            int settings_enable_connect_protocol;
            int settings_no_rfc7540_priorities;
            int tls_reneg_permitted;
            int settings_enable_metadata;
        };

        struct parse_vars_t {
            ssize_t *tmp;
            bool next_line_state = false;
            std::string buffer{};
            ssize_t i = 0;
            ssize_t buffint = 0;
            ssize_t nkey = 0;
            std::string key{};
            ssize_t size = 0;
            ssize_t j = 0;
        };

        struct protocol_http2_error_t {
            int errnum = 0;
            int last_stream_id = 0;
            std::string errmsg{};
        };

        struct protocol_http2_window_t {
            ssize_t read = 0;
            ssize_t write = 0;

            int stream_window = 0;
            int conn_window = 0;
        };

        struct protocol_http2_t {
            ssize_t length = 9 + 8; // 9 must-have octets in the header + 8 metadata
            ssize_t type = 0;
            int stream_id = 0;
            uint8_t flag = 0;
            http_v2_settings_t client_settings;
            http_v2_settings_t server_settings;
            ssize_t padding = 0;
            std::chrono::system_clock::time_point prev_ping_time_point = std::chrono::system_clock::now();
            std::chrono::milliseconds ping_delay {200};
            int conn_type = 0;
            std::set <std::string> pings;

            protocol_http2_window_t window{};
            std::optional<protocol_http2_error_t> error{};

            int value = -1;
            int rst_cnt = 0;
            ssize_t timeout = 1000;
            ssize_t current_timeout = timeout;
            std::queue <manapi::timer> setting_timeout{};
            manapi::compress::hpack::decoder_t decoder{};
            manapi::compress::hpack::encoder_t encoder{};

            int last_stream_id = 0;
            size_t setting_param_acks = 0;
        };

    public:
        struct http_v2_thread_data_t {
            int id;
            int conn_flags;
            std::atomic<int> atomic_flags;

            http2_conn_type type;

            ssize_t write_window;
            ssize_t read_window;

            ssize_t write_cursor;

            std::map <std::string, std::string> headers;

            manapi::object_item_pool<bytebuffer, std::size_t> write_buffer;
            manapi::object_item_pool<bytebuffer, std::size_t> read_buffer;

            std::unique_ptr<http_v2_write_buffers> read_storage_buffer;
            ssize_t read_storage_size;
            http_v2_write_buffers*read_storage_last;
            ssize_t read_freed;

            manapi::async::mutex mx;
            int transfered_last_delay;
        };

        struct http_v2_callbacks_t {
            std::function<void(int id)> headers;
            std::function<void(int id)> data;
            std::function<void(int last_stream_id, int errnum, std::string errmsg)> goaway;
            std::function<void(int id, int prioritized_id, std::string prioritized_value)> priority_update;
            std::function<void(std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator, int errnum)> rst_stream;
            std::function<void(int id)> finished;
        };

        http_v2 (const std::shared_ptr<manapi::net::worker::TCP> &worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        ~http_v2() override;

        void update_setting (http2_setting_type type, int value, bool self);
        void set_watcher_event (int revents);
        void remove_watcher_event (int revents);
        future<void> parse_request(ssize_t j, ssize_t size);
        void handle_callback_watcher ();
        void init_settings();
        void init_callbacks();
        void set_callbacks(const http_v2_callbacks_t &callbacks);

        ssize_t sync_read(worker::connection *conn, void *buff, ssize_t size) override;
        ssize_t sync_write(worker::connection *conn, const void *buff, ssize_t size) override;
        void stop() override;
        future<bool> configure_connection(std::shared_ptr<connection> conn) override;
        void connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) override;
        void init() override;
        bool is_valid_connection(worker::connection &connection) override;
        void onrecv(std::shared_ptr<ev::io> &watcher, int status, int revents) override;

        future<ssize_t> response (worker::connection &connection, http::response &resp, bool finish) override;

        std::shared_ptr<worker::connection> connection;
        manapi::object_item_pool<bytebuffer, std::size_t> buffer{};
        std::function<std::shared_ptr<manapi::net::worker::http_v2>()> new_dependency;
        std::chrono::steady_clock::time_point start;
    private:
        ssize_t buffer_size ();
        void empty_setting_timeouts ();
        void generate_error (http2_error_type errnum, std::string errmsg, int last_stream_id = 0) noexcept(false);

        void _skip_sm_msg (char &c);
        void _next_line (char &c);
        void _skip_null_octet (char &c);
        void _parse_header_octets (char &c);
        void _parse_header_length (char &c);
        void _parse_header_type (char &c);
        void _parse_header_flag (char &c);
        void _parse_header_stream_id (char &c);

        void _parse_goaway_last_stream_id (char &c);
        void _parse_goaway_error_code (char &c);
        void _parse_goaway_additional_debug_data (char &c);

        void _parse_window_update_value (char &c);
        void _parse_rst_stream_action (char &c);

        void _parse_ping_data (char &c);

        void _parse_setting_id (char &c);
        void _parse_setting_value (char &c);

        void _parse_header_data (char &c);

        void _parse_body_data (char &c);

        void _parse_skip_n_bytes (char &c);

        void _parse_field_block (char &c);

        void _parse_number (char &c);

        void exec_callback (char &c);
        void flush_write_buffer ();
        void init_write_buffer ();
        void send_frame (http2_frame_type frame, uint8_t flag, int stream_id, std::string_view data);
        void send_empty_frame (http2_frame_type frame, char flag, int stream_id);
        void timer_watcher (const std::shared_ptr<manapi::net::worker::base> &dep);
        bool send_ping_frame (std::string data={});
        void close_http2_connection (int errnum = HTTP2_ERROR_NO_ERROR, std::string additional_data = "");

        void send_settings (const std::vector <std::pair <short, int>> &options);
        ssize_t send_data (std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator stream, const void *buf, ssize_t size, bool finish);
        void send_window_frame (int stream_id, int size);
        void resolve_timeout_timer ();

        void default_ev_headers (int id);
        void default_ev_data (int id);
        void default_ev_goaway (int last_stream_id, int errnum, std::string errmsg);
        void default_ev_finished (int id);
        void default_ev_priopity_update (int id, int prioritized_id, std::string prioritized_value);
        void default_ev_rst_stream (std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator, int errnum);
        void unlimit_all_streams ();
        void reset_all_streams ();
        void delete_stream_id (int id);
        void reset_stream (int id, int errnum);
        void session_worker (std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator it);
        std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator flush_io_stream (std::map<int, std::unique_ptr<http_v2_thread_data_t>>::iterator it);
        void flush_io_streams ();
        void io_call_callback (std::shared_ptr<ev::async> &w);
        void send_headers (http_v2_thread_data_t &stream);
        void enable_status_io (http_v2_thread_data_t &stream);
        void disable_status_io (http_v2_thread_data_t &stream);

        future<ssize_t> default_read (worker::connection &connection, void *buff, ssize_t size);
        future<ssize_t> default_write (worker::connection &connection, const void *buff, ssize_t size, bool flag);

        static void stringify_stream_id (int stream_id, char *buffer);
        void setting_param_was_ack (const bool &self);

        void setting_value_valid (const http2_setting_type &type, const int &value) noexcept(false);

        template <typename T>
        static void stringify_number (T n, char *buffer, int size = sizeof (T)) {
            int index = 0;
            for (int i = sizeof (n) - 1 - (sizeof (T) - size); i >= 0; --i) {
                buffer[index++] = static_cast<char> ((n >> i * 8) & 0xFF);
            }
        }

        std::string headerbuffer{};
        parse_vars_t parse_vars;

        std::shared_ptr<ev::async> io_call_watcher;

        ssize_t write_buffer_current;
        ssize_t write_buffer_cursor;
        ssize_t write_buffer_size;
        std::unique_ptr<http_v2_write_buffers> write_buffer;
        http_v2_write_buffers *write_buffer_last;

        protocol_http2_t protocol;

        int current, next;
        http_v2_callbacks_t callbacks{};
        std::shared_ptr<worker::TCP> worker;

        async::promise<void, std::false_type>::resolve_t http2_resolve_;

        std::map <int, std::unique_ptr<http_v2_thread_data_t>> threads;

        manapi::timer ping_interval{};
        static std::map <int, json_mask> allow_settings;
    };
}
