#pragma once

#include <ev++.h>
#include <netdb.h>

#include "./Base.hpp"
#include "ManapiAsync.hpp"
#include "http/HeaderView.hpp"

namespace manapi::net::worker {
    class TCP : public worker::base {
    public:
        struct connection_stat_interface {
            std::atomic<size_t> total_write = 0;
            std::atomic<size_t> total_read = 0;
            size_t last_total_write = 0;
            size_t last_total_read = 0;
            size_t time_ms = 0;
        };
        struct connection_interface {
            manapi::async::mutex iomutex;
            int id{};
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

        struct connection_io_await {
            std::function<void()> &iohandle;
            std::atomic<int> &iostatus;
            async::mutex &mx;
            int status;
            void await_resume () noexcept {}
            bool await_ready () noexcept { return this->iostatus & CONN_CLOSED; }
            template<typename T>
            requires(std::is_base_of_v<promise_base, T>)
            void await_suspend (std::coroutine_handle<T> handle) {
                if (this->iostatus & CONN_CLOSED) {
                    this->mx.unlock();
                    future<>::resume_promise(handle);
                }
                else {
                    this->iohandle = [handle = std::exchange(handle, nullptr)]() -> void {
                        future<>::resume_promise(handle);
                    };
                    this->mx.unlock();
                    this->iostatus.fetch_or(this->status);
                }
            }
        };

        TCP (net::site &site);
        TCP (TCP && n) noexcept;
        ~TCP ();
        bool is_valid_connection(worker::connection &connection) override;
        void init ();
        future<bool> configure_connection (std::shared_ptr<worker::connection> connection);
        future<ssize_t> response(worker::connection &connection, http_response &resp, bool finish) override;
        TCP &operator=(TCP &&n) noexcept;
        void disable_watcher_for_status(connection &conn, const connection_status &status) override;
        void onevent(ev::io &watcher, int revents);
        void onrecv(ev::io &watcher, int revents) override;
        static std::shared_ptr<worker::TCP> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);
        std::optional<std::shared_ptr<manapi::net::worker::connection>> accept (const std::function<std::shared_ptr<connection>()> &init);
        std::optional<std::shared_ptr<manapi::net::worker::connection>> accept ();
        future<void> connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) override;
    protected:
        virtual void _recv_setup_connection (manapi::net::worker::connection &storage);
        static future<void> io_wait (connection_interface &conn, const int &status);
        void _timeout (std::shared_ptr<connection> storage, const int &revents) override;
        void _ev_watcher_stop (connection_interface & conn);
        static void _ev_timeout (EV_P_ ev_timer *w, int revents);
        void _connection_close (std::shared_ptr<connection> conn, connection_interface &connection);
        struct async_stack_storage {
            std::shared_ptr<future<void>> stack;
            std::shared_ptr<http::HeaderView> storage;
        };


        virtual void _lookup_event (ev::io &watcher, std::shared_ptr<connection> storage, const int &revents);
        void _io_event (std::shared_ptr<connection> storage, int revents);
        static void _connection_interface_eraser (connection_interface *connection);

        std::map <int, std::shared_ptr<async_stack_storage>> stacks;

        future<ssize_t> default_write (connection &conn, const void *buff, const size_t &size) const;
        future<ssize_t> default_read (connection &conn, void *buff, const size_t &size) const;
    private:
        std::string stringify_http_info (manapi::net::http_response &res, const http::versions::http &version, const std::string &delimiter) const;
        std::string stringify_headers (manapi::net::http_response &res, const std::string &delimiter) const;
        static void connection_interface_eraser (void *ptr);



        std::stack<std::shared_ptr<async_stack_storage>> tmp;
        addrinfo *local;
        int socket_param_true = 1;
        int socket_param_false = 0;
        timeval recv_timeout{}, send_timeout{};
        addrinfo hints{};
    };
}
