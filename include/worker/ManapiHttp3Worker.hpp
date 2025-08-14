#pragma once


#include "./ManapiBaseWorker.hpp"
#include "../std/ManapiBuffer.hpp"
#include "./ManapiBaseUtils.hpp"

namespace manapi::net::worker {
    enum http3_error_type {
        HTTP3_ERROR_NO_ERROR = 0x0100,
        HTTP3_ERROR_GENERAL_PROTOCOL_ERROR = 0x0101,
        HTTP3_ERROR_INTERNAL_ERROR = 0x0102,
        HTTP3_ERROR_STREAM_CREATION_ERROR = 0x0103,
        HTTP3_ERROR_CLOSED_CRITICAL_STREAM = 0x0104,
        HTTP3_ERROR_FRAME_UNEXPECTED = 0x0105,
        HTTP3_ERROR_FRAME_ERROR = 0x0106,
        HTTP3_ERROR_EXCESSIVE_LOAD = 0x0107,
        HTTP3_ERROR_ID_ERROR = 0x0108,
        HTTP3_ERROR_SETTINGS_ERROR = 0x0109,
        HTTP3_ERROR_MISSING_SETTINGS = 0x010a,
        HTTP3_ERROR_REQUEST_REJECTED = 0x010b,
        HTTP3_ERROR_REQUEST_CANCELLED = 0x010c,
        HTTP3_ERROR_REQUEST_INCOMPLETE = 0x010d,
        HTTP3_ERROR_MESSAGE_ERROR = 0x010e,
        HTTP3_ERROR_CONNECT_ERROR = 0x010f,
        HTTP3_ERROR_VERSION_FALLBACK = 0x0110
    };

    struct http_v3_stream_base_t : connection_prepared_t {
    };

    struct http_v3_callbacks_t {
        ssize_t (*http_v3_write) (const worker::shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT;
        int (*http_v3_on_read_stream) (const worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT;
        int (*http_v3_want_write)(const worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT;
        int (*http_v3_rst_stream) (const worker::shared_conn &conn, int code) MANAPIHTTP_NOEXCEPT;
        bool (*http_v3_is_writable) (const worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT;
        manapi::net::worker::connection::ipdata_t *(*http_v3_ip_data) (manapi::net::worker::connection *conn) MANAPIHTTP_NOEXCEPT;
    };

    /**
     * flush read buffers to the event callback.
     *
     * @param conn
     * @param s
     * @return ERR_OK on success, otherwise it returns ERR_ABORTED
     */
    int http_v3_flush_recv (http::config *config, const manapi::net::worker::shared_conn &conn, manapi::net::worker::http_v3_stream_base_t *s) MANAPIHTTP_NOEXCEPT;

    /**
     * HTTP/3 for workers, which supports a multistream
     * worker::connection in this class provides a QUIC stream interface,
     * so user must provides http_v3_stream_base_t in the wrk.data field
     */
    class http_v3 final : public worker::base {
    public:
        http_v3 (worker::base *w, http_v3_callbacks_t *callbacks);

        ~http_v3 () override;

        const std::shared_ptr<multithread_storage::worker_t> &worker_data() MANAPIHTTP_NOEXCEPT override;

        wrk_interface_global_t *wrk_global() MANAPIHTTP_NOEXCEPT override;

        void wrk_global(wrk_interface_global_t *data) MANAPIHTTP_NOEXCEPT override;

        http::config *config() MANAPIHTTP_NOEXCEPT override;

        http::site &site() MANAPIHTTP_NOEXCEPT override;

        void waiting(const shared_conn &conn, bool state) MANAPIHTTP_NOEXCEPT override;

        void feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) MANAPIHTTP_NOEXCEPT override;

        void close_connection(shared_conn conn, int flags) MANAPIHTTP_NOEXCEPT override;

        int event_flags(const shared_conn & conn) MANAPIHTTP_NOEXCEPT override;

        int event_flags(const shared_conn & conn, int flags) MANAPIHTTP_NOEXCEPT override;

        worker_watcher_cb event_on(const shared_conn & conn, worker_watcher_cb callback) MANAPIHTTP_NOEXCEPT override;

        manapi::future<manapi::error::status> init(std::size_t deep) override;

        connection::ipdata_t *ipdata(worker::connection *conn) MANAPIHTTP_NOEXCEPT override;

        bool is_writable(const shared_conn &conn) MANAPIHTTP_NOEXCEPT override;

        void stop(std::function<void()> cb) override;

        ssize_t sync_write(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) MANAPIHTTP_NOEXCEPT override;

        /* size must always be -1 */
        ssize_t sync_write_ex(const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) MANAPIHTTP_NOEXCEPT override;

        void update_limit_rate_stream (const shared_conn &conn) MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::size_t recv_count(const shared_conn &conn) const MANAPIHTTP_NOEXCEPT override;

        bytebuffer recv_first_buffer(const shared_conn &conn) MANAPIHTTP_NOEXCEPT override;
    private:
        http_v3_callbacks_t *callbacks;
        worker::base *w;
    };
}
