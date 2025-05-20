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

#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"
#include "async/ManapiAsyncConditionVariable.hpp"
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

        void max_header_block_size (const size_t &s);
        [[nodiscard]] size_t &max_header_block_size ();

        [[nodiscard]] size_t &partial_data_min_size ();

        bool contains_http_version (int version);
        int recommended_http_version ();
        std::set<int> &http_versions ();

        void keep_alive (const long int &seconds);
        [[nodiscard]] size_t &keep_alive ();

        size_t &max_connections ();
        int &max_backlog();

        void port (const std::string &_port);
        [[nodiscard]] std::string &port ();

        [[nodiscard]] std::string &implementation ();
        [[nodiscard]] std::string &transport ();
        [[nodiscard]] std::string &address ();
        [[nodiscard]] std::string &quic_implement ();
        [[nodiscard]] std::string &cipher_list();

        [[nodiscard]] size_t &tls_version ();

        [[nodiscard]] bool &is_quic_debug ();

        [[nodiscard]] size_t &quic_cc_algo ();

        [[nodiscard]] size_t &max_buffer_stack ();

        ssize_t &max_rst_cnt ();
        ssize_t &speed_check_delay ();
        ssize_t &speed_check_bytes ();
        ssize_t &speed_limit_rate ();

        ssl_config_t &ssl_config ();

        void server_address (const sockaddr &addr);
        sockaddr &server_address ();
        void server_len (const size_t &len);
        [[nodiscard]] socklen_t &server_len ();

        [[nodiscard]] bool contains_compressor (const std::string &name);
        void function_contains_compressor (std::move_only_function<bool(const std::string &name)> func);

        static const std::string &stringify_http_version (const int &version);

        static http::versions::http parse_http_version (const std::string &version);

        bool &tcp_no_delay ();

        bool &simultaneous_accepts ();

        bool &verify_peer ();

        ssize_t &buffer_size ();
    private:
        // settings
        bool simultaneous_accepts_;
        bool quic_debug_;
        size_t quic_cc_algo_;
        size_t tls_version_;
        size_t max_header_block_size_;
        size_t partial_data_min_size_;
        std::set<int> http_versions_ = {};
        std::string address_;
        std::string port_;// settings
        std::string implementation_;
        std::string transport_;
        size_t max_buffer_stack_;
        size_t keep_alive_;
        sockaddr server_addr_;
        socklen_t server_len_;
        size_t max_plain_param_length_;
        size_t max_file_param_length_;
        size_t max_connections_;
        int max_backlog_;
        ssize_t buffer_size_;
        ssize_t max_rst_cnt_;
        ssize_t speed_check_delay_;
        /* 80KB */
        ssize_t speed_check_bytes_;
        /* 2000 MB */
        ssize_t speed_limit_rate_;
        ssl_config_t ssl_config_;
        bool tcp_no_delay_;
        bool verify_peer_;
        std::move_only_function<bool(const std::string &name)> function_contains_compressor_ = nullptr;
        std::string cipher_list_;
    };
}