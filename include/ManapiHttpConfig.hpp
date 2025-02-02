#pragma once

#include <string>
#include <functional>
#if defined(_WIN32)
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <winsock2.h>
#   include <ws2tcpip.h>
#endif
#if defined(__unix__)||defined(__APPLE__)
#   include <sys/socket.h>
#endif

#include "ManapiJson.hpp"
#include "components/Atomic.hpp"

namespace manapi::net::http {
    struct ssl_config_t {
        bool            enabled = false;
        std::string     key;
        std::string     cert;
    };

    namespace versions {
        enum tls {
            TLS_v1 = 0,
            TLS_v1_1 = 1,
            TLS_v1_2 = 2,
            TLS_v1_3 = 3,
            DTLS_v1 = 4,
            DTLS_v1_2 = 5,
            DTLS_v1_3 = 6
        };

        enum {
            QUIC_CC_NONE = 0,
            QUIC_CC_RENO = 1,
            QUIC_CC_CUBIC = 2,
            QUIC_CC_BBR = 3,
            QUIC_CC_BBR2 = 4
        };

        enum http {
            HTTP_v0_9 = 0,
            HTTP_v1_0 = 1,
            HTTP_v1_1 = 2,
            HTTP_v2 = 3,
            HTTP_v3 = 4
        };
    }

    class config {
    public:
        config (const json &config);
        ~config ();

        void set_socket_block_size (const size_t &s);
        [[nodiscard]] std::atomic<size_t> &get_socket_block_size ();

        void set_max_header_block_size (const size_t &s);
        [[nodiscard]] std::atomic<size_t> &get_max_header_block_size ();

        [[nodiscard]] std::atomic<size_t> &get_partial_data_min_size ();

        void set_http_version (const size_t &new_http_version);
        [[nodiscard]] std::atomic<size_t> &get_http_version ();

        void set_http_version_str (const std::string &new_http_version);
        [[nodiscard]] AtomicReference<std::string> get_http_version_str ();

        void set_keep_alive (const long int &seconds);
        [[nodiscard]] std::atomic<size_t> &get_keep_alive ();

        [[nodiscard]] std::atomic<ssize_t> &get_recv_timeout ();
        [[nodiscard]] std::atomic<ssize_t> &get_send_timeout ();

        std::atomic<size_t> &max_connections ();
        std::atomic<int> &max_backlog();

        void set_port (const std::string &_port);
        [[nodiscard]] AtomicReference<std::string> get_port ();

        [[nodiscard]] AtomicReference<std::string> get_implementation ();
        [[nodiscard]] AtomicReference<std::string> get_transport ();
        [[nodiscard]] AtomicReference<std::string> get_address ();
        [[nodiscard]] AtomicReference<std::string> get_quic_implement ();

        [[nodiscard]] std::atomic<size_t> &get_tls_version ();

        [[nodiscard]] std::atomic<bool> &is_quic_debug ();

        [[nodiscard]] std::atomic<size_t> &get_quic_cc_algo ();

        std::atomic<ssize_t> &max_rst_cnt ();
        std::atomic<ssize_t> &speed_check_delay ();
        std::atomic<ssize_t> &speed_check_bytes ();
        std::atomic<ssize_t> &speed_limit_rate ();

        AtomicReference<ssl_config_t> get_ssl_config ();

        void set_server_address (const sockaddr &addr);
        AtomicReference<sockaddr> get_server_address ();
        void set_server_len (const size_t &len);
        [[nodiscard]] std::atomic<socklen_t> &get_server_len ();

        // void set_http3_config (quiche_h3_config *config);
        // quiche_h3_config *get_http3_config ();
        //
        // void set_quic_config (quiche_config *config);
        // quiche_config *get_quic_config ();
#if defined(_WIN32)
        void set_socket_fd (const SOCKET &fd);
        std::atomic <SOCKET> &get_socket_fd ();
#else
        void set_socket_fd (const int &fd);
        std::atomic <int> &get_socket_fd ();
#endif

        [[nodiscard]] bool contains_compressor (const std::string &name);
        void set_function_contains_compressor (const std::function<bool(const std::string &name)> &func);

        static const std::string &stringify_http_version (const versions::http &version);
        static http::versions::http parse_http_version (const std::string &version);

        std::atomic<bool> &get_tcp_no_delay ();

        std::atomic<bool> &get_verify_peer ();

        std::atomic<ssize_t> &buffer_size ();
    private:
        // settings
        std::atomic<bool> quic_debug = false;
        std::atomic<size_t> quic_cc_algo = versions::QUIC_CC_RENO;
        std::atomic<size_t> tls_version = versions::TLS_v1_3;
        std::atomic<size_t> max_header_block_size = 4096UL;
        std::atomic<size_t> socket_block_size = 1350UL;
        std::atomic<size_t> partial_data_min_size = 0UL;
        std::atomic<size_t> http_version = versions::HTTP_v1_1;
        Atomic<std::string> http_version_str = "1.1";
        Atomic<std::string> address = "0.0.0.0";
        Atomic<std::string> port = "8888";// settings
        Atomic<std::string> implementation = "default";
        Atomic<std::string> transport = "tcp";
        std::atomic<size_t> keep_alive = 2UL;
        Atomic<sockaddr> server_addr;
        std::atomic<socklen_t> server_len;
        std::atomic<size_t> max_plain_param_length  = 16000UL;
        std::atomic<size_t> max_file_param_length   = 2147483648UL;
        std::atomic<size_t> max_connections_{1000};
        std::atomic<int> max_backlog_{200};
        std::atomic<ssize_t> buffer_size_{65536};
        std::atomic<ssize_t> max_rst_cnt_{5};
        std::atomic<ssize_t> speed_check_delay_ {200};
        /* 80KB */
        std::atomic<ssize_t> speed_check_bytes_ {81920};
        /* 2000 MB */
        std::atomic<ssize_t> speed_limit_rate_ {2097152000};
#ifdef _WIN32
        std::atomic<SOCKET> sock_fd{0};
#else
        std::atomic<int> sock_fd{0};
#endif
        std::atomic<ssize_t> recv_timeout = 1000;
        std::atomic<ssize_t> send_timeout = 1000;
        Atomic<ssl_config_t> ssl_config;
        std::atomic<bool> tcp_no_delay = false;
        std::atomic<bool> verify_peer = true;
        std::function<bool(const std::string &name)> function_contains_compressor = nullptr;
    };
}