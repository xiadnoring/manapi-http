#pragma once

#include <thread>

#include "ManapiUtils.hpp"
#include "http/ManapiBaseHttp.hpp"
#include "compress/ManapiHPack.hpp"
#include "worker/ManapiHttp2Worker.hpp"

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

    struct http_v2_t;

    struct http_v2_stream_t : worker::http_v2_stream_base_t {

        int id;

        http_v2_t *ctx;

        ssize_t content_length;

        int write_window;
        int read_window;

        std::unique_ptr<request_data_t> req;

        uint8_t priority;
    };

    struct http_v2_t {
        worker::shared_conn conn;
        worker::base *worker;
        worker::http_v2 *http_v2_worker;
        //int (*newstream_cb)(http_v2_t *ctx, int stream_id);

        char flags;

        // private
        uint8_t current;
        uint8_t next;

        int n2;
        uint32_t pos1;
        uint32_t n1;
        uint32_t frame_length;
        uint8_t frame_type;
        uint8_t frame_flag;
        uint32_t frame_stream_id;
        uint32_t last_stream_id;

        uint32_t read_window;
        uint32_t write_window;

        uint16_t concurrent_streams_size;

        std::string frame_buffer;

        std::unique_ptr<http_v2_settings_t> client;
        std::unique_ptr<http_v2_settings_t> server;

        std::map<uint32_t, worker::shared_conn> streams;
        std::size_t streams_size;

        std::unique_ptr<manapi::compress::hpack::decoder_t> decoder;
        std::unique_ptr<manapi::compress::hpack::encoder_t> encoder;

        manapi::timer timeout;

        std::unique_ptr<std::map<std::pair<uint8_t, int>, worker::shared_conn >> priorities;
        std::unique_ptr<std::set<std::string>> pings;
        uint16_t status;
    };

    enum http2_stream_flags {
        HTTP2_STREAM_WANT_READ = manapi::ev::READ,
        HTTP2_STREAM_WANT_WRITE = manapi::ev::WRITE,
        HTTP2_STREAM_CLOSED = manapi::ev::DISCONNECT,
        HTTP2_STREAM_REMOVED = manapi::net::worker::base::CONN_REMOVED,
        HTTP2_STREAM_RECV_END = worker::base::CONN_RECV_END,
        HTTP2_STREAM_SEND_END  = manapi::net::worker::base::CONN_SEND_END,
        HTTP2_STREAM_IO_WAITING = worker::base::CONN_IO_WAITING,
        HTTP2_STREAM_TOP_READ = worker::base::CONN_TOP_READ,
        HTTP2_STREAM_PRIORITY_INCR = manapi::net::worker::base::CONN_MAX_CODE << 1,
        HTTP2_STREAM_PRIORITY_LOCKED = manapi::net::worker::base::CONN_MAX_CODE <<2,
        HTTP2_STREAM_BLOCK_WRITE = manapi::net::worker::base::CONN_MAX_CODE <<3,
        HTTP2_STREAM_BAD_STATUS = manapi::net::worker::base::CONN_MAX_CODE <<4,
        HTTP2_STREAM_WINDOW_EMPTY = manapi::net::worker::base::CONN_MAX_CODE <<5,
        HTTP2_STREAM_RECV_DATA_END = manapi::net::worker::base::CONN_MAX_CODE <<6,
        HTTP2_STREAM_RECV_END_FLAG = manapi::net::worker::base::CONN_MAX_CODE <<8,
        HTTP2_STREAM_STARTED = manapi::net::worker::base::CONN_MAX_CODE<<9,
        HTTP2_STREAM_RST_BY_PEER = manapi::net::worker::base::CONN_MAX_CODE<<10
    };


    enum http2_ctx_flags {
        HTTP2_CTX_FLAG_BLOCK_WRITE = 1,
        HTTP2_CTX_FLAG_WANT_CLOSE = 2,
        HTTP2_CTX_FLAG_REALY_CLOSE = 4,
        HTTP2_CTX_FLAG_NEW_CONNECTION = 8
    };

    enum http_v2_errs {
        EHTTP_V2_PROTOCOL_OK = 0,
        EHTTP_V2_PROTOCOL_WANT_READ,
        EHTTP_V2_PROTOCOL_ERROR,
        EHTTP_V2_IO_ERROR,
        EHTTP_V2_NEW_STREAM
    };

    int http_v2_on_closing (http_v2_t *ctx) MANAPIHTTP_NOEXCEPT;

    int http_v2_on_close (http_v2_t *ctx) MANAPIHTTP_NOEXCEPT;

    int http_v2_on_close_stream (http_v2_t *ctx, int id) MANAPIHTTP_NOEXCEPT;

    int http_v2_on_write (http_v2_t *ctx) MANAPIHTTP_NOEXCEPT;

    int http_v2_on_read_stream (const worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT;

    int http_v2_work (http_v2_t *ctx, http::config *config, const char **nbuffer, ssize_t *nsize) MANAPIHTTP_NOEXCEPT;

    ssize_t http_v2_write (const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT;

    int http_v2_rst_stream (const worker::shared_conn &s, int errcode) MANAPIHTTP_NOEXCEPT;

    manapi::future<int> http_v2_response (worker::base *worker, const worker::shared_conn &connection, int status, std::map<std::string, std::string, std::less<>> headers, bool finish);
}
