#pragma once

#include <memory>
#include <functional>
#include <ev++.h>

#include "../ManapiHttpConfig.hpp"
#include "../ManapiHttpResponse.hpp"
#include "../ManapiSite.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::net::worker {
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
        http::versions::http version = http::versions::HTTP_v1_1;
    private:
        std::unique_ptr<void, void(*)(void *)> ptr;
    };

    class base {
    public:
        enum connection_status {
            CONN_IDLE   = 0b00000001,
            CONN_WRITE  = 0b00000010,
            CONN_READ   = 0b00000100,
            CONN_CLOSED = 0b00001000
        };

        base (net::site &site);
        base (base &&n) noexcept;
        virtual ~base ();

        virtual bool is_valid_connection (worker::connection &connection);
        virtual void init ();
        virtual void set_config (std::shared_ptr<manapi::net::http::config> config);
        virtual void connection_close (std::shared_ptr<connection> conn);
        virtual void disable_watcher_for_status (connection &conn, const connection_status &status);

        virtual future<bool> configure_connection (std::shared_ptr<connection> conn);

        virtual std::optional<std::shared_ptr<manapi::net::worker::connection>> accept (const std::function<std::shared_ptr<connection>()> &init);
        virtual std::optional<std::shared_ptr<manapi::net::worker::connection>> accept ();

        virtual void onrecv (const std::shared_ptr<worker::base> &worker);

        base &operator= (base &&n) noexcept;

        void set_fd_non_blocking (int fd);

        virtual future<ssize_t> response (worker::connection &connection, http_response &resp, bool finish);
        static std::shared_ptr<base> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);

        std::function<future<ssize_t>(connection &conn, const void *buff, const size_t &size, bool finish)> write;
        std::function<future<ssize_t>(connection &conn, void *buff, const size_t &size)> read;

        ev::loop_ref loop = nullptr;
        net::site &site;
        std::shared_ptr<manapi::net::http::config> config;
        std::atomic<int> cnt_conns = 0;
    };
}