#ifndef MANAPIHTTP_HTTP_HTTPV2_HPP
#define MANAPIHTTP_HTTP_HTTPV2_HPP

#include <thread>

#include "../ManapiUtils.hpp"
#include "./base_http.hpp"
#include "../compress/ManapiHPack.hpp"

namespace manapi::net::worker {
    class http_v2;
}

namespace manapi::net::http {
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

    enum http2_stream_flags {
        HTTP2_STREAM_WANT_READ = manapi::ev::READ,
        HTTP2_STREAM_WANT_WRITE = manapi::ev::WRITE,
        HTTP2_STREAM_CLOSED = manapi::ev::DISCONNECT,
        HTTP2_STREAM_PRIORITY_INCR = 8,
        HTTP2_STREAM_REMOVED = 16,
        HTTP2_STREAM_SEND_END  = 32,
        HTTP2_STREAM_RECV_END = worker::base::CONN_RECV_END,
        HTTP2_STREAM_PRIORITY_LOCKED = 128,
        HTTP2_STREAM_IO_WAITING = 256
    };

    enum http2_ctx_flags {
        HTTP2_CTX_FLAG_BLOCK_WRITE = 1,
        HTTP2_CTX_FLAG_WANT_CLOSE = 2,
        HTTP2_CTX_FLAG_REALY_CLOSE = 4
    };

    struct http_v2_t;

    struct http_v2_stream_t {
        ssize_t transfered_k;

        int id;
        int flags;

        http_v2_t *ctx;

        int write_window;
        int read_window;

        std::unique_ptr<worker::base::connection_io_part> recv;
        int recv_size;

        std::unique_ptr<request_data_t> req;
        std::unique_ptr<worker::worker_watcher_cb> ev_callback;

        int speed_min_delay;

        uint8_t priority;
    };

    struct http_v2_t {
        worker::shared_conn conn;
        worker::base *worker;
        std::shared_ptr<worker::http_v2> http_v2_worker;

        char flags;

        // private
        uint8_t current;
        uint8_t next;

        int n2;
        unsigned int pos1;
        unsigned int n1;
        unsigned int frame_length;
        int frame_type;
        int frame_flag;
        int frame_stream_id;
        int last_stream_id;

        int read_window;
        int write_window;

        int concurrent_streams_size;

        std::string frame_buffer;

        std::unique_ptr<http_v2_settings_t> client;
        std::unique_ptr<http_v2_settings_t> server;

        std::unique_ptr<std::map<int, worker::shared_conn>> streams;

        std::unique_ptr<manapi::compress::hpack::decoder_t> decoder;
        std::unique_ptr<manapi::compress::hpack::encoder_t> encoder;

        manapi::timer timeout;

        std::unique_ptr<std::set<std::pair<uint8_t, int>>> priorities;
        std::unique_ptr<std::set<std::string>> pings;
    };

    enum http_v2_errs {
        EHTTP_V2_PROTOCOL_OK = 0,
        EHTTP_V2_PROTOCOL_WANT_READ,
        EHTTP_V2_PROTOCOL_ERROR,
        EHTTP_V2_IO_ERROR,
        EHTTP_V2_NEW_STREAM
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

    int http_v2_on_close (http_v2_t *ctx);
    int http_v2_on_close_stream (http_v2_t *ctx, int id);
    int http_v2_on_write (http_v2_t *ctx);
    int http_v2_on_read_stream (const worker::shared_conn &conn, http_v2_stream_t *s);
    int http_v2_work (http_v2_t *ctx, http::config *config, const char **nbuffer, ssize_t *nsize);
    ssize_t http_v2_write (const worker::shared_conn &conn, http_v2_stream_t *s, const void *buffer, ssize_t size, bool finish);
    int http_v2_rst_stream (http_v2_stream_t *s, int errcode);
    manapi::future<ssize_t> http_v2_response (worker::base *worker, const worker::shared_conn &connection, http_v2_stream_t *s, int status, std::map<std::string, std::string> headers, bool finish);
}

#endif //MANAPIHTTP_HTTP_HTTPV2_HPP
