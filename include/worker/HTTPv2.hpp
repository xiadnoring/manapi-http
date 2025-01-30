#pragma once

#include <thread>

#include "../ManapiSite.hpp"
#include "../components/SmartBuffer.hpp"
#include "./base_worker.hpp"
#include "../compress/ManapiHPack.hpp"

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

    struct http_v2_session_t {
        int id;
        bool body;
        std::map <std::string, std::string> headers;
        http2_conn_type type = HTTP2_CONN_IDLE;
    };

    struct http_v2_callbacks_t {
        std::function<manapi::future<void>(int id, std::map <std::string, std::string> headers)> headers;
        std::function<void(int id)> data;
        std::function<void(int last_stream_id, int errnum, std::string errmsg)> goaway;
        std::function<void(int id, int prioritized_id, std::string prioritized_value)> priority_update;
        std::function<void(int id, int errnum)> rst_stream;
        std::function<void(int id)> finished;
    };

    struct http_v2_thread_data_t {
        int id;
        std::map <std::string, std::string> headers;
        bool rst = false;

        std::shared_ptr<smart_w_buffer> write;
        std::shared_ptr<smart_r_buffer> read;
    };

    class http_v2 : public worker::base {
        struct parse_vars_t {
            bool next_line_state = false;
            std::string buffer;
            size_t i = 0;
            size_t buffint = 0;
            size_t nkey;
            std::string key;
            size_t size;
            size_t j = 0;
        };

        struct protocol_http2_t {
            async::mutex mx; // multithread
            ssize_t length = 9 + 8; // 9 must-have octets in the header + 8 metadata
            ssize_t type = 0;
            int stream_id = 0;
            uint8_t flag = 0;
            bool initial_frame = true;
            std::map <int, std::pair <std::atomic<int>, std::function <void(int value, bool self)>>> settings;
            size_t padding = 0;
            ssize_t timer_interval = 20;
            std::chrono::system_clock::time_point prev_ping_time_point = std::chrono::system_clock::now();
            std::chrono::milliseconds ping_delay {200};
            std::atomic<int> conn_type = 0;
            std::set <std::string> pings;

            struct protocol_http2_window_t {
                std::shared_ptr<async::condition_variable> write_cv{};

                std::atomic<ssize_t> read = 0;
                std::atomic<ssize_t> write = 0;
            } window{};

            struct protocol_http2_error_t {
                int errnum = 0;
                int last_stream_id = 0;
                std::string errmsg{};
            } error{};

            int value = -1;
            std::atomic<int> rst_cnt = 0;
            ssize_t timeout = 1000;
            std::atomic<ssize_t> current_timeout = timeout;
            std::queue <size_t> setting_timeout{};
            manapi::future<> parse_exception{nullptr};
            manapi::compress::hpack::decoder_t decoder{};
            manapi::compress::hpack::encoder_t encoder{};

            std::shared_ptr<async::mutex> setting_param_acks_mx{nullptr};
            std::atomic<size_t> setting_param_acks = 0;
        };
    public:
        http_v2 (const std::shared_ptr<manapi::net::worker::base> &worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        ~http_v2() override;

        future<void> parse_request(ssize_t j, ssize_t size);
        void init_settings();
        void init_callbacks();
        void set_callbacks(const http_v2_callbacks_t &callbacks);

        future<ssize_t> response (worker::connection &connection, http_response &resp, bool finish) override;

        std::shared_ptr<worker::connection> connection;
        std::string buffer;
        std::function<std::shared_ptr<manapi::net::worker::http_v2>()> new_dependency;
        std::chrono::steady_clock::time_point start;
    private:
        void _deps_decrease();
        future<void> empty_setting_timeouts ();
        future<void> generate_error (http2_error_type errnum, std::string errmsg, int last_stream_id = 0) noexcept(false);

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

        void _parse_skip_n_bytes (char &c, size_t &n);

        void _parse_field_block (char &c);

        void _parse_number (char &c, size_t &num, size_t &length);

        future<void> send_frame (http2_frame_type frame, uint8_t flag, int stream_id, std::string_view data);
        future<void> send_empty_frame (http2_frame_type frame, char flag, int stream_id);
        future<void> timer_watcher ();
        future<void> send_ping_frame (std::string data={});
        future<void> close_connection (int errnum = HTTP2_ERROR_NO_ERROR, std::string additional_data = "", int last_stream_id = 0);

        future<void> send_settings (const std::vector <std::pair <short, int>> &options);
        future<ssize_t> send_data (int stream_id, const void *buf, ssize_t size, bool finish);
        future<void> send_window_frame (int stream_id, int size);
        void resolve_timeout_timer ();

        future<void> default_ev_headers (int id, std::map <std::string, std::string> headers);
        void default_ev_data (int id);
        void default_ev_goaway (int last_stream_id, int errnum, std::string errmsg);
        void default_ev_finished (int id);
        void default_ev_priopity_update (int id, int prioritized_id, std::string prioritized_value);
        void default_ev_rst_stream (int id, int errnum);
        future<void> unlimit_all_streams ();
        future<void> reset_all_streams ();
        future<void> delete_stream_id (const int &id);
        future<void> reset_stream (int id, int errnum);
        void session_worker (int id, bool body, std::shared_ptr<smart_w_buffer> write, std::shared_ptr<smart_r_buffer> read);

        future<ssize_t> default_read (worker::connection &connection, void *buff, ssize_t size);
        future<ssize_t> default_write (worker::connection &connection, const void *buff, ssize_t size, bool flag);

        static std::string stringify_stream_id (int stream_id);
        void setting_param_was_ack (const bool &self);

        void settings_update_initial_window_size (int value);
        void settings_update_max_concurrent_streams (int value);
        void setting_value_valid (const http2_setting_type &type, const int &value) noexcept(false);

        template <typename T>
        std::string stringify_number (T n) {
            std::string result;
            result.reserve(sizeof (n));
            for (int i = sizeof (n) - 1; i >= 0; i--) {
                result += static_cast<char> ((n >> i * 8) & 0xFF);
            }
            return std::move(result);
        }

        parse_vars_t parse_vars;

        protocol_http2_t protocol;

        std::map <int, http_v2_session_t> sessions;

        std::function<void(char&)> current, next;
        http_v2_callbacks_t callbacks{};
        std::shared_ptr<worker::base> worker;

        async::mutex threads_mutex;
        std::map <int, http_v2_thread_data_t> threads;
        std::atomic<size_t> thread_cnt;

        async::condition_variable finishcv;
        std::atomic<size_t> ping_interval = 0;
        std::atomic<size_t> deps = 0;

        static std::map <int, json_mask> allow_settings;
    };
}
