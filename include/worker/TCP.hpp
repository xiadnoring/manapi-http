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
            std::shared_ptr<ev::io> watcher;
        };
#pragma pack(pop)

#pragma pack(push,16)
        struct connection_interface {
            sd_t id{};
            manapi::timer t;
            net::site *site;
            std::shared_ptr<worker::base> worker;
            connection_stat_interface stats;
            bool configured = false;
            std::atomic<int> status = 0x0;
            async::cancellation_action iocancel;
            std::shared_ptr<ev::io> watcher;
        };
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
        ~TCP () override;
        bool is_valid_connection(worker::connection &connection) override;
        void init () override;
        future<bool> configure_connection (std::shared_ptr<worker::connection> connection) override;
        future<ssize_t> response(worker::connection &connection, http::response &resp, bool finish) override;
        void onrecv(ev::io &watcher, int revents) override;
        static std::shared_ptr<worker::TCP> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);
        std::optional<std::shared_ptr<manapi::net::worker::connection>> accept (const std::function<std::shared_ptr<connection>()> &init);
        std::optional<std::shared_ptr<manapi::net::worker::connection>> accept ();
        void connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) override;
        void stop() override;
        int status (connection &conn) override;
        void connection_shutdown(std::shared_ptr<connection> conn, bool connection_status) override;
        void connection_cancel(std::shared_ptr<connection> conn) override;
        ssize_t sync_read(worker::connection *conn, void *buff, ssize_t size) override;
        ssize_t sync_write(worker::connection *conn, const void *buff, ssize_t size) override;
        manapi::future<std::shared_ptr<ev::io>> async_watch_io(worker::connection *conn, int revents, std::move_only_function<void(ev::io &w, int revents)> callback) override;
        std::shared_ptr<ev::io> sync_watch_io(worker::connection *conn, int revents, std::move_only_function<void(ev::io &w, int revents)> callback) override;
    protected:
        virtual void recv_setup_connection (manapi::net::worker::connection &storage);
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
        std::string stringify_http_info (manapi::net::http::response &res, const int &version, const std::string &delimiter) const;
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
