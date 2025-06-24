#pragma once

#include "./ManapiBaseWorker.hpp"
#include "../components/ManapiBuffer.hpp"

namespace manapi::net::worker {
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

    struct http_v2_stream_base_t {
        int flags;
        int recv_size;
        ssize_t transfered_k;
        int speed_min_delay;
        std::unique_ptr<worker::worker_watcher_cb> ev_callback;
        std::unique_ptr<worker::connection_io_part> recv;
    };

    struct http_v2_callbacks_t {
        ssize_t (*http_v2_write) (const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish);
        int (*http_v2_on_read_stream) (const worker::shared_conn &conn);
        int (*http_v2_want_write)(const worker::shared_conn &conn);
        int (*http_v2_rst_stream) (const worker::shared_conn &conn, int code);
        bool (*http_v2_is_writable) (const worker::shared_conn &conn);
        manapi::net::worker::connection::ipdata_t *(*http_v2_ip_data) (manapi::net::worker::connection *conn);
    };

    class http_v2 final : public worker::base {
    public:
        http_v2 (worker::base *w, http_v2_callbacks_t *callbacks);

        ~http_v2 ();

        const std::shared_ptr<worker_config_t> &worker_data() override;

        wrk_interface_global_t *wrk_global() override;

        void wrk_global(wrk_interface_global_t *data) override;

        http::config *config() override;

        http::site &site() override;

        void waiting(const shared_conn &conn, bool state) override;

        void feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) override;

        void close_connection(shared_conn conn, bool clean_disconnect) override;

        void configure_connection(const shared_conn &conn, oncont_cb cb) override;

        int event_flags(const shared_conn & conn) override;

        int event_flags(const shared_conn & conn, int flags) override;

        std::unique_ptr<worker_watcher_cb> event_on(const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) override;

        void init() override;

        connection::ipdata_t *ipdata(worker::connection *conn) override;

        bool is_writable(const shared_conn &conn) override;

        bool is_valid_connection(worker::connection *connection) override;

        void stop(std::function<void()> cb) override;

        ssize_t sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) override;

        /* size must always be -1 */
        ssize_t sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) override;

        void update_limit_rate_stream (const shared_conn &conn);
    private:
        http_v2_callbacks_t *callbacks;
        worker::base *w;
    };
}
