#ifndef MANAPIHTTP_WORKER_BASE_HPP
#define MANAPIHTTP_WORKER_BASE_HPP

#include <memory>
#include <functional>

#include "ManapiHttpConfig.hpp"
#include "ManapiHttpResponse.hpp"
#include "ManapiUtils.hpp"

namespace manapi::net::worker {
    class connection {
    public:
        connection (void *ptr, void(*eraser)(void*));
        connection (connection &&n) noexcept;
        connection &operator= (connection &&n) noexcept;

        template <typename T>
        T &as () {
            const auto pointer = ptr.get();
            if (pointer == nullptr) { THROW_MANAPI_EXCEPTION2(ERR_FATAL, "Pointer is null"); }
            return *reinterpret_cast<T *> (pointer);
        }

        sockaddr_storage client{};
        socklen_t len{};
        http::versions::http version = http::versions::HTTP_v1_1;
    private:
        std::unique_ptr<void, void(*)(void *)> ptr;
    };

    class base {
    public:
        base ();
        base (base &&n) noexcept;
        virtual ~base ();

        virtual bool is_valid_connection (worker::connection &connection);
        virtual void init ();
        virtual void set_config (std::shared_ptr<manapi::net::http::config> config);
        virtual bool configure_connection (connection &conn) const;

        virtual connection accept ();

        base &operator= (base &&n) noexcept;

        virtual ssize_t response (worker::connection &connection, http_response &resp, bool finish);

        std::function<ssize_t(connection &conn, const void *buff, const size_t &size, bool finish)> write;
        std::function<ssize_t(connection &conn, void *buff, const size_t &size)> read;
    protected:
        std::shared_ptr<manapi::net::http::config> config;
    };
}

#endif //MANAPIHTTP_WORKER_BASE_HPP
