#pragma once

#include <netdb.h>
#include "./Base.hpp"
#include "ManapiAsync.hpp"
#include "http/HeaderView.hpp"

namespace manapi::net::worker {
    class TCP : public worker::base {
    public:
        TCP (net::site &site);
        TCP (TCP && n) noexcept;
        ~TCP ();
        bool is_valid_connection(worker::connection &connection) override;
        void init ();
        bool configure_connection (worker::connection &connection) const;
        future<ssize_t> response(worker::connection &connection, http_response &resp, bool finish) override;
        TCP &operator=(TCP &&n) noexcept;
        void onrecv(const std::shared_ptr<worker::base> &worker) override;
        static std::shared_ptr<worker::TCP> create (net::site &site, std::shared_ptr<manapi::net::http::config> config);
        std::pair <bool, std::shared_ptr<manapi::net::worker::connection>> accept (const std::function<std::shared_ptr<connection>()> &init);
        std::pair <bool, std::shared_ptr<manapi::net::worker::connection>> accept ();
    protected:
        struct async_stack_storage {
            future<void> stack;
            std::unique_ptr<http::HeaderView> storage;
        };

        Atomic<std::map <int, async_stack_storage>> stacks;
    private:
        std::string stringify_http_info (manapi::net::http_response &res, const http::versions::http &version, const std::string &delimiter) const;
        std::string stringify_headers (manapi::net::http_response &res, const std::string &delimiter) const;
        static void connection_interface_eraser (void *ptr);

        virtual bool established (worker::connection &conn, bool flag) const;

        future<ssize_t> default_write (connection &conn, const void *buff, const size_t &size) const;
        future<ssize_t> default_read (connection &conn, void *buff, const size_t &size) const;

        addrinfo *local;
        int so_reuseaddr_param = 1;
        timeval recv_timeout{}, send_timeout{};
        addrinfo hints{};
    };
}
