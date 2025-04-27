#pragma once

#include <memory>
#include <functional>

#include "../ManapiUtils.hpp"
#include "../ManapiHttpConfig.hpp"
#include "../ManapiHttpResponse.hpp"
#include "../ManapiSite.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::net::worker {
#pragma pack(push,16)

    class connection {
    public:
        connection (void *ptr, void(*eraser)(void*));
        connection (connection &&n) noexcept;
        connection &operator= (connection &&n) noexcept;

        template <typename T>
        T &as () {
            const auto pointer = static_cast <T *> (ptr.get());
            if (pointer == nullptr) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "Pointer is null");
            }
            return *pointer;
        }

        sockaddr_storage client{};
        socklen_t len{};
        int version = http::versions::HTTP_v1_1;
    private:
        std::unique_ptr<void, void(*)(void *)> ptr;
    };

    class base {
    public:
        enum connection_status {
            CONN_IDLE           = 0b00000001,
            CONN_WRITE          = 0b00000010,
            CONN_READ           = 0b00000100,
            CONN_CLOSED         = 0b00001000,
            CONN_HALF_CLOSED    = 0b00010000,
            CONN_LIMIT_RATE     = 0b00100000
        };

        enum sync_want_error {
            IO_FATAL_ERROR = -1,
            IO_WANT_READ = -10001,
            IO_WANT_WRITE = -10002,
            IO_WANT_AGAIN = -10003
        };

        base (net::site &site);

        virtual ~base ();

        virtual bool is_valid_connection (worker::connection &connection) = 0;
        virtual void init () = 0;
        virtual void set_config (std::shared_ptr<manapi::net::http::config> config);
        virtual void connection_close (std::shared_ptr<connection> conn, bool clean_disconnect) = 0;

        virtual future<bool> configure_connection (std::shared_ptr<connection> conn) = 0;

        virtual void onrecv (std::shared_ptr<ev::io> &watcher, int status, int revents) = 0;

        manapi::future<ssize_t> fwrite (connection &conn, const void *buff, ssize_t size, bool finish);
        manapi::future<ssize_t> fread (connection &conn, void *buff, ssize_t size);

        virtual future<ssize_t> response (worker::connection &connection, http::response &resp, bool finish);

        virtual void stop () = 0;

        virtual ssize_t sync_write (worker::connection *conn, const void *buff, ssize_t size) = 0;
        virtual ssize_t sync_read (worker::connection *conn, void *buff, ssize_t size) = 0;

        std::function<future<ssize_t>(connection &conn, const void *buff, ssize_t size, bool finish)> write;
        std::function<future<ssize_t>(connection &conn, void *buff, ssize_t size)> read;

        std::shared_ptr<event_loop> le{nullptr};
        std::shared_ptr<ev::io> watcher;
        std::shared_ptr<ev::async> async_watcher;
        std::weak_ptr<worker::base> worker;
        net::site &site;
        std::shared_ptr<manapi::net::http::config> config;
        ssize_t count;
    };
#pragma pack(pop)

}