#ifndef MANAPIHTTP_WORKER_TLS_HPP
#define MANAPIHTTP_WORKER_TLS_HPP

#include <netdb.h>

#include "worker/Base.hpp"

namespace manapi::net::worker {
    class TLS : public worker::base {
    public:
        TLS ();
        TLS (TLS && n) noexcept;
        ~TLS ();
        bool is_valid_connection(worker::connection &connection) override;
        void init ();
        bool configure_connection (worker::connection &connection) const;
        ssize_t response(worker::connection &connection, http_response &resp, bool finish) override;
        TLS &operator=(TLS &&n) noexcept;

        connection accept ();
    private:
        std::string stringify_http_info (manapi::net::http_response &res, const http::versions::http &version, const std::string &delimiter) const;
        std::string stringify_headers (manapi::net::http_response &res, const std::string &delimiter) const;
        static void connection_interface_eraser (void *ptr);
        static SSL_CTX* ssl_create_context (const size_t &version = http::versions::TLS_v1_3);
        void ssl_configure_context ();

        bool established (worker::connection &conn, bool flag = false) const;

        ssize_t ssl_write (connection &conn, const void *buff, const size_t &size) const;
        ssize_t ssl_read (connection &conn, void *buff, const size_t &size) const;

        ssize_t default_write (connection &conn, const void *buff, const size_t &size) const;
        ssize_t default_read (connection &conn, void *buff, const size_t &size) const;

        addrinfo *local;


        int so_reuseaddr_param = 1;
        timeval recv_timeout{}, send_timeout{};
        addrinfo hints{};
    };
}

#endif //MANAPIHTTP_WORKER_TLS_HPP
