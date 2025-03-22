#pragma once

#include "../ManapiUtils.hpp"
#include "../extensions/ev++.h"

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

#include "./base_worker.hpp"
#include "./ManapiAsync.hpp"
#include "../http/HeaderView.hpp"
#include "../async/ManapiCancellation.hpp"

namespace manapi::net::worker {
    class TCP : public worker::base {
    public:
#pragma pack(push,16)
        struct connection_stat_interface {
            std::atomic<ssize_t> total_write = 0;
            std::atomic<ssize_t> total_read = 0;
            std::atomic<ssize_t> transfared_last_second = 0;
            size_t last_total_write = 0;
            size_t last_total_read = 0;

            size_t time_ms = 0;
        };
#pragma pack(pop)

#pragma pack(push,16)
#ifdef _WIN32
        struct connection_interface {
            manapi::async::mutex iomutex;
            SOCKET id{};
            std::shared_ptr <ev::io> watcher;
            ev_timer timer;
            net::site *site;
            std::shared_ptr<worker::base> worker;
            connection_stat_interface stats;
            bool configured = false;
            std::atomic<int> status = 0x0;
            std::atomic<int> mustly = 0b11111111;
            std::function<void()> iohandle;

            std::function<void(std::shared_ptr<connection> connection, int revents)> handle;
        };
#else
        struct connection_interface {
            manapi::async::mutex iomutex;
            int id{};
            manapi::timer t;
            net::site *site;
            std::shared_ptr<worker::base> worker;
            connection_stat_interface stats;
            bool configured = false;
            std::atomic<int> status = 0x0;
            std::atomic<int> mustly = 0b11111111;
            async::cancellation_action iocancel;
        };
#endif
#pragma pack(pop)

        struct connection_io_await {
            std::function<void()> &iohandle;
            std::atomic<int> &iostatus;
            int status;
            void await_resume () noexcept {}
            bool await_ready () noexcept { return this->iostatus & CONN_CLOSED; }
            template<typename T>
            requires(std::is_base_of_v<promise_base, T>)
            void await_suspend (std::coroutine_handle<T> handle) {
                if (this->iostatus & CONN_CLOSED) {
                    future<>::resume_promise(handle);
                }
                else {
                    this->iohandle = [handle]() -> void {
                        future<>::resume_promise(handle);
                    };
                    if (this->iostatus.fetch_or(this->status) & CONN_CLOSED) {
                        this->iohandle=nullptr;
                        future<>::resume_promise(handle);
                    }
                }
            }
        };

        TCP (net::site &site);
        TCP (TCP && n) noexcept;
        ~TCP ();
        bool is_valid_connection(worker::connection &connection) override;
        void init ();
        future<bool> configure_connection (std::shared_ptr<worker::connection> connection);
        future<ssize_t> response(worker::connection &connection, http::response &resp, bool finish) override;
        TCP &operator=(TCP &&n) noexcept;
        void disable_watcher_for_status(connection &conn, const connection_status &status) override;
        void onrecv(ev::io &watcher, int revents) override;
        static std::shared_ptr<worker::TCP> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);
        std::optional<std::shared_ptr<manapi::net::worker::connection>> accept (const std::function<std::shared_ptr<connection>()> &init);
        std::optional<std::shared_ptr<manapi::net::worker::connection>> accept ();
        future<void> connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) override;
        void stop() override;
        int status (connection &conn) override;
    protected:
        virtual void _recv_setup_connection (manapi::net::worker::connection &storage);
        void update_limit_rate ();
        void _timeout (std::shared_ptr<connection> storage) override;
        void _ev_watcher_stop (connection_interface & conn);
        void _connection_close (std::shared_ptr<connection> conn, connection_interface &connection);
        struct async_stack_storage {
            std::shared_ptr<future<void>> stack;
            std::shared_ptr<http::HeaderView> storage;
        };


        virtual void update_limit_rate_connection (connection &conn);
        static void _connection_interface_eraser (connection_interface *connection);


        future<ssize_t> default_write (connection &conn, const void *buff, ssize_t size) const;
        future<ssize_t> default_read (connection &conn, void *buff, ssize_t size) const;

        std::map <int, std::shared_ptr<async_stack_storage>> stacks;
        std::shared_ptr<async::condition_variable> limit_rate_cv;
    private:
        std::string stringify_http_info (manapi::net::http::response &res, const http::versions::http &version, const std::string &delimiter) const;
        std::string stringify_headers (manapi::net::http::response &res, const std::string &delimiter) const;
        static void connection_interface_eraser (void *ptr);

        addrinfo *local;
        timer limit_rate_timer{};
#ifdef _WIN32
        char socket_param_true = 1;
        char socket_param_false = 0;
#else
        int socket_param_true = 1;
        int socket_param_false = 0;
#endif
        timeval recv_timeout{}, send_timeout{};
        addrinfo hints{};
    };
}
