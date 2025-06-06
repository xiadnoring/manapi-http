#pragma once

#include <memory>
#include <functional>

#include "../ManapiUtils.hpp"
#include "../ManapiHttpConfig.hpp"
#include "../ManapiSite.hpp"

namespace manapi::net::worker {
    struct sockaddr_st {
        char data[std::max(sizeof (struct sockaddr_in), sizeof (struct sockaddr_in6))];
    };

    class connection {
    public:
        struct ipdata_t {
            worker::sockaddr_st client{};
            socklen_t len{};
        };

        connection (void *ptr, void(*eraser)(void*));

        template <typename T>
        T *as () {
            if (const auto pointer = static_cast <T *> (this->ptr.get())) {
                return pointer;
            }
            THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "Pointer is null");
        }

        manapi::async::cancellation_action cancellation;
        std::unique_ptr<ipdata_t> ipdata;
        int version = http::versions::HTTP_v1_1;
    private:
        std::unique_ptr<void, void(*)(void *)> ptr;
    };

    using shared_conn = std::shared_ptr<worker::connection>;
    using ibuffpool_t = object_item_pool<bytebuffer, std::size_t>;
    using worker_watcher_cb = std::move_only_function<void(const shared_conn &conn, int flags, const char *buffer, ssize_t nsize, ibuffpool_t *p)>;

    enum net_worker_flags {
        NET_WORKER_CLOSED = 1
    };

    class base {
    public:
        struct connection_base_t {
            ssize_t transfered;
            ssize_t transfered_k;
        };

        using bufferpool_t = std::shared_ptr<object_pool<bytebuffer, std::false_type, std::size_t>>;

        struct buffer_deque {
            object_item_pool<bytebuffer, std::size_t> buffer;
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
            connection_io_part recv;
            int recv_size;
        };

        typedef vbefore_delete<bool, false> oncont_cb;

        enum connection_status {
            CONN_READ           = 0b00000001,
            CONN_WRITE          = 0b00000010,
            CONN_CLOSED         = 0b00000100,
            CONN_REMOVED        = 0b00001000,
            CONN_KEEP_ALIVE     = 0b00010000,
            CONN_LIMIT_RATE     = 0b00100000,
            CONN_RECV_END       = 0b01000000,
            CONN_RESERVED       = 0b10000000
        };

        enum connection_io_status {
            CONN_IO_OK = 0,
            CONN_IO_ERROR = -1,
            CONN_IO_WANT_READ = -1000,
            CONN_IO_WANT_WRITE = -1001
        };

        base (net::http::site site, std::shared_ptr<worker::worker_config_t> worker_data, manapi::net::http::config *config);

        base (net::http::site site, bufferpool_t bufferpool, std::shared_ptr<worker::worker_config_t> worker_data, manapi::net::http::config *config);

        virtual ~base ();

        virtual bool is_valid_connection (worker::connection *connection) = 0;

        virtual void init () = 0;

        virtual void close_connection (shared_conn conn, bool clean_disconnect) = 0;

        virtual void configure_connection (const shared_conn &conn, oncont_cb cb) = 0;

        virtual ssize_t sync_write_ex (const shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt) = 0;

        virtual ssize_t sync_write (const shared_conn &conn, const void *buff, ssize_t size, bool finish) = 0;

        virtual bool is_writable (const shared_conn &conn) = 0;

        virtual void waiting (const shared_conn &conn, bool state) = 0;

        manapi::future<ssize_t> write (const shared_conn &conn, const void *buff, ssize_t size, bool finish);

        manapi::future<ssize_t> fwrite (const shared_conn &conn, const void *buff, ssize_t size, bool finish);

        virtual future<ssize_t> response (const shared_conn &connection, http::response *resp, bool finish);

        virtual void stop (std::function<void()> cb) = 0;

        virtual std::unique_ptr<worker_watcher_cb> event_on (const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) = 0;

        virtual int event_flags (const shared_conn & conn, int flags) = 0;

        virtual int event_flags (const shared_conn & conn) = 0;

        virtual void feed_event (const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) = 0;

        void event_toggle (const shared_conn & conn, bool state, int flag);

        virtual connection::ipdata_t *ipdata (worker::connection *conn);

        net::http::site &site ();

        net::http::config *config ();

        const bufferpool_t &bufferpool();

        const std::shared_ptr<worker_config_t> &worker_data ();

        static void connection_io_merge (struct connection_io_part *dest, struct connection_io_part *src, int *dest_cnt, int *src_cnt, int max_cnt);

        static ssize_t connection_io_recv (struct connection_io_part *top, char *buffer, ssize_t size, int *cnt);

        static ssize_t connection_io_send (struct connection_io_part *top, const char *buffer, ssize_t size, object_pool<bytebuffer, std::false_type, std::size_t> *bufferpool, int buffer_size, int *cnt, int max_cnt);

        static void connection_io_trim (struct connection_io_part *top, buffer_deque *parent, int *cnt);
    protected:

        std::shared_ptr<worker::worker_config_t> worker_data_;

        net::http::site site_;
        manapi::net::http::config *config_;
        bufferpool_t bufferpool_;
    };

    using shared_worker = std::shared_ptr<worker::base>;
    using unique_conn = std::unique_ptr<worker::connection>;

}