#pragma once

#include <memory>
#include <functional>

#include "../ManapiUtils.hpp"
#include "../ManapiHttpConfig.hpp"
#include "../ManapiSite.hpp"

namespace manapi::net::worker {
    struct sockaddr_st {
        char data[std::max(sizeof (struct sockaddr_in),
            sizeof (struct sockaddr_in6))];
    };

    class connection {
    public:
        connection (void *ptr, void(*eraser)(void*));

        template <typename T>
        T *as () {
            if (const auto pointer = static_cast <T *> (this->ptr.get())) {
                return pointer;
            }
            THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "Pointer is null");
        }

        worker::sockaddr_st client{};
        socklen_t len{};
        int version = http::versions::HTTP_v1_1;
    private:
        std::unique_ptr<void, void(*)(void *)> ptr;
    };

    typedef std::shared_ptr<worker::connection> shared_conn;
    typedef object_item_pool<bytebuffer, std::size_t> ibuffpool_t;
    typedef std::move_only_function<void(const worker::shared_conn &conn, int flags, ibuffpool_t buffer)> worker_watcher_cb;

    class base {
    public:
        typedef std::shared_ptr<object_pool<bytebuffer, std::false_type, std::size_t>> bufferpool_t;
        struct connection_stat_interface {
            size_t transfared_last_second = 0;
        };

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
            CONN_IDLE           = 0b00001000,
            CONN_HALF_CLOSED    = 0b00010000,
            CONN_LIMIT_RATE     = 0b00100000
        };

        enum connection_io_status {
            CONN_IO_OK = 0,
            CONN_IO_ERROR = -1,
            CONN_IO_WANT_READ = -1000,
            CONN_IO_WANT_WRITE = -1001
        };

        base (net::site &site);

        virtual ~base ();

        virtual bool is_valid_connection (worker::connection *connection) = 0;

        virtual void init () = 0;

        virtual void config (std::shared_ptr<manapi::net::http::config> config);

        virtual void close_connection (worker::connection* conn, bool clean_disconnect) = 0;

        virtual void configure_connection (const shared_conn &conn, oncont_cb cb) = 0;

        virtual ssize_t sync_write (const shared_conn &conn, const void *buff, ssize_t size, bool finish) = 0;

        manapi::future<ssize_t> write (const shared_conn &conn, const void *buff, ssize_t size, bool finish);

        manapi::future<ssize_t> fwrite (const shared_conn &conn, const void *buff, ssize_t size, bool finish);

        virtual future<ssize_t> response (const shared_conn &connection, http::response *resp, bool finish);

        virtual void stop () = 0;

        virtual std::unique_ptr<worker_watcher_cb> event_on (worker::connection *conn, std::unique_ptr<worker_watcher_cb> callback) = 0;

        std::unique_ptr<worker_watcher_cb> event_on (worker::connection *conn, worker_watcher_cb callback);

        virtual int event_flags (worker::connection *conn, int flags) = 0;

        virtual int event_flags (worker::connection *conn) = 0;

        void event_toggle (worker::connection *conn, bool state, int flag);

        net::site &site ();

        net::http::config *config ();

        const bufferpool_t &bufferpool();
    protected:
        static void connection_io_merge (struct connection_io_part *dest, struct connection_io_part *src, int *dest_cnt, int *src_cnt, int max_cnt);

        static ssize_t connection_io_recv (struct connection_io_part *top, char *buffer, ssize_t size, int *cnt);

        static ssize_t connection_io_send (struct connection_io_part *top, const char *buffer, ssize_t size, object_pool<bytebuffer, std::false_type, std::size_t> *bufferpool, int buffer_size, int *cnt, int max_cnt);

        static void connection_io_trim (struct connection_io_part *top, buffer_deque *parent);
    private:
        net::site site_;
        std::shared_ptr<manapi::net::http::config> config_;
        bufferpool_t bufferpool_;
    };

    typedef std::shared_ptr<worker::base> shared_worker;
    typedef std::unique_ptr<worker::connection> unique_conn;

}