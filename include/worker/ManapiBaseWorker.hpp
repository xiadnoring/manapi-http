#pragma once

#include <memory>
#include <functional>

#include "../ManapiHttpConfig.hpp"
#include "../http/ManapiSite.hpp"

namespace manapi::net::worker {
    struct sockaddr_st {
        char data[std::max(sizeof (struct sockaddr_in), sizeof (struct sockaddr_in6))];
    };

    template<typename T>
    void http_wrk_data_deleter (void *pointer) {
        delete static_cast<T *>(pointer);
    }

    enum wrk_interface_flags {
        WRK_INTERFACE_CUSTOM_READ = 1,
        WRK_INTERFACE_CUSTOM_RATE_LIMIT = 2,
        WRK_INTERFACE_CONN_RETRY = 4,
        WRK_INTERFACE_TCP_KEEP_ALIVE = 8
    };

    enum close_flags_t {
        CLOSE_CONN_EOF = 1,
        CLOSE_CONN_ERR = 2,
        CLOSE_CONN_SHUTDOWN = 4
    };

    struct wrk_interface_t {
        uint8_t flags;
        void *data;
    };

    class connection {
    public:
        struct ipdata_t {
            worker::sockaddr_st client{};
            socklen_t len{};
        };

        connection (void *ptr);

        template <typename T>
        T *as () {
            if (const auto pointer = static_cast <T *> (this->ptr)) {
                return pointer;
            }
            throw manapi::exception (ERR_INTERNAL, "Pointer is null");
        }

        manapi::async::cancellation_action cancellation;
        std::unique_ptr<ipdata_t> ipdata;
        int version = http::versions::HTTP_v1_1;
        wrk_interface_t wrk;
    private:
        void *ptr;
    };

    using shared_conn = std::shared_ptr<worker::connection>;
    using ibuffpool_t = bytebuffer;
    using worker_watcher_cb = std::move_only_function<void(const shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p)>;

    struct wrk_interface_global_t {
        void *data;
        int (*init_cb)(const worker::shared_conn &conn, wrk_interface_global_t *global, worker::base *w);
        int (*cleanup_cb)(worker::connection *conn, wrk_interface_global_t *global, worker::base *w);
        int (*cleanup_global_cb)(wrk_interface_global_t *data, worker::base *w);
        void (*flush_custom_read_cb)(const worker::shared_conn &conn, wrk_interface_global_t *global, worker::base *w);
        int (*accept_cb)(const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p, wrk_interface_global_t *global, worker::base *w);
        void (*custom_read_cb)(const worker::shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p, wrk_interface_global_t *global, worker::base *w);
        void (*update_limit_rate)(const manapi::net::worker::shared_conn &conn, wrk_interface_global_t *global, worker::base *w);
        manapi::future<int> (*send_response)(const manapi::net::worker::shared_conn &conn, wrk_interface_global_t *global, worker::base *w, http::response* res, bool finish);
    };

    enum net_worker_flags {
        NET_WORKER_CLOSED = 1
    };

    struct buffer_deque {
        bytebuffer buffer;
        std::unique_ptr<buffer_deque> next;
    };

    struct connection_io_part {
        int deque_current;
        int deque_cursor;
        std::unique_ptr<buffer_deque> deque;
        buffer_deque *last_deque;
    };

    struct connection_io {
        connection_io_part send;
        int send_size;
        int cur_send_size;
        connection_io_part recv;
        int recv_size;
    };

    struct connection_base_t;

    class base {
    public:

        typedef std::map<uintptr_t, shared_conn> conn_by_port;

        typedef std::map<std::array<char, 17>, conn_by_port> conns_by_ip;

        typedef vbefore_delete<bool, false> oncont_cb;

        enum connection_status {
            CONN_READ           = manapi::ev::READ,
            CONN_WRITE          = manapi::ev::WRITE,
            CONN_CLOSED         = 4,
            CONN_REMOVED        = 8,
            CONN_RECV_END       = 16,
            CONN_SEND_END       = 32,
            CONN_IO_WAITING     = 64,
            CONN_TOP_READ       = 128,

            CONN_MASK_UPDATE    = CONN_READ | CONN_WRITE | CONN_RECV_END | CONN_SEND_END,
            CONN_MASK_GETTING   = CONN_READ | CONN_WRITE | CONN_CLOSED | CONN_RECV_END | CONN_SEND_END
        };

        enum connection_io_status {
            CONN_IO_OK = 0,
            CONN_IO_ERROR = -1,
            CONN_IO_WANT_READ = -1000,
            CONN_IO_WANT_WRITE = -1001,
            CONN_IO_AGAIN = -1002,
        };

        enum stream_flags {
            /**
             * says to create a new unidirectional stream
             * instead of a bidirectional stream
             */
            CONN_STREAM_FLAG_UNI
        };

        base ();

        virtual ~base ();

        virtual void wrk_global (wrk_interface_global_t *data) = 0;

        virtual wrk_interface_global_t *wrk_global () = 0;

        virtual http::site &site() = 0;

        virtual http::config *config() = 0;

        virtual const std::shared_ptr<multithread_storage::worker_t> & worker_data() = 0;

        virtual manapi::future<error::status> init (std::size_t deep) = 0;

        virtual void close_connection (shared_conn conn, int flags) = 0;

        virtual ssize_t sync_write_ex (const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, ssize_t size, bool finish, int maxcnt) = 0;

        virtual ssize_t sync_write_ex (const shared_conn &conn, manapi::slice_view buffs, bool finish, int maxcnt);

        virtual ssize_t sync_write (const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish) = 0;

        ssize_t sync_write_ex (const shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt);

        ssize_t sync_write (const shared_conn &conn, slice_view buffs, bool finish);

        ssize_t sync_write (const shared_conn &conn, const void *buff, ssize_t size, bool finish);

        virtual bool is_writable (const shared_conn &conn) = 0;

        virtual void waiting (const shared_conn &conn, bool state) = 0;

        manapi::future<ssize_t> write (const shared_conn &conn, const void *buff, ssize_t size, bool finish);

        manapi::future<ssize_t> write (const shared_conn &conn, ev::buff_t *buff, uint32_t nbuff, bool finish);

        manapi::future<ssize_t> write (const shared_conn &conn, manapi::slice_view buffs, bool finish);

        manapi::future<ssize_t> fwrite (const shared_conn &conn, const void *buff, ssize_t size, bool finish);

        manapi::future<ssize_t> fwrite (const shared_conn &conn, manapi::slice_view slice, bool finish);

        [[nodiscard]] virtual std::size_t recv_count (const shared_conn &conn) const = 0;

        virtual bytebuffer recv_first_buffer (const shared_conn &conn) = 0;

        virtual void stop (std::function<void()> cb) = 0;

        virtual std::unique_ptr<worker_watcher_cb> event_on (const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) = 0;

        virtual int event_flags (const shared_conn & conn, int flags) = 0;

        virtual int event_flags (const shared_conn & conn) = 0;

        virtual void feed_event (const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) = 0;

        void event_toggle (const shared_conn & conn, bool state, int flag);

        /**
         * Get the ipdata structure from the connection object |conn|
         * @param conn connection object
         * @return reference to the ipdata structure
         */
        virtual connection::ipdata_t *ipdata (worker::connection *conn);

        /**
         * Get the app bufferpool
         * @return
         */
        object_pool &bufferpool();

        /**
         * Obtain the stream identifier from the stream |s|
         * @param s stream
         * @return always 0 if streams not supported, otherwise it returns a stream id
         */
        virtual std::size_t stream_id (shared_conn s) MANAPIHTTP_NOEXPECT;

        /**
         * Create a new stream in |conn|
         *
         * @param flags stream flags
         * @return a stream connection on success, otherwise it returns Unimplemeneted, InternalError, ResourceExhausted
         */
        virtual error::status_or<shared_conn> new_stream (shared_conn conn, base::stream_flags flags) MANAPIHTTP_NOEXPECT;

        /**
         * Close the provided stream in |conn|
         * @param conn connection
         * @param s stream
         * @return Ok on success, otherwise it returns Unimplemented, InternalError
         */
        virtual error::status close_stream (shared_conn conn, shared_conn s) MANAPIHTTP_NOEXPECT;

        static void connection_io_merge (struct connection_io_part *dest, struct connection_io_part *src, int *dest_cnt, int *src_cnt, int max_cnt);

        static ssize_t connection_io_recv (struct connection_io_part *top, char *buffer, ssize_t size, int *cnt);

        static ssize_t connection_io_send (struct connection_io_part *top, const char *buffer, ssize_t size, object_pool *bufferpool, int buffer_size, int *cnt, int max_cnt);

        static void connection_io_send_start (struct connection_io_part *top, const char *buffer, ssize_t size, object_pool *bufferpool, int buffer_size, ibuffpool_t *buff, int *cnt);

        static void connection_io_trim (struct connection_io_part *top, buffer_deque *parent, int *cnt);

        static ssize_t buffs_cut_by_size (ev::buff_t *buff, uint32_t &nbuff, ssize_t limit_size, bool &fin);

        static int call_user_callback (const std::unique_ptr<worker_watcher_cb> &cb, const shared_conn & conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p);

        void feed_event_read_ (const shared_conn &conn, worker_watcher_cb *cb, connection_io_part *recv, int *recv_size, int conn_flags, int flags, const char *buff, ssize_t size, ibuffpool_t *p);
    protected:

    };

    using shared_worker = std::shared_ptr<worker::base>;
    using unique_conn = std::unique_ptr<worker::connection>;

}