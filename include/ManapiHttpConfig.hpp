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

        bool contains_http_version (int version);
        int recommended_http_version ();

        [[nodiscard]] bool contains_compressor (const std::string &name);
        void function_contains_compressor (std::move_only_function<bool(const std::string &name)> func);

        static const std::string &stringify_http_version (const int &version);

        static http::versions::http parse_http_version (const std::string &version);

        // settings
        bool simultaneous_accepts;
        bool quic_debug;
        size_t quic_cc_algo;
        size_t tls_version;
        size_t max_header_block_size;
        size_t partial_data_min_size;
        std::set<int> http_versions = {};
        std::string address;
        std::string port;// settings
        std::string implementation;
        std::string transport;
        size_t max_buffer_stack;
        size_t keep_alive;
        sockaddr server_addr;
        socklen_t server_len;
        size_t max_plain_param_length;
        size_t max_file_param_length;
        size_t max_connections;
        int max_backlog;
        ssize_t buffer_size;
        ssize_t max_rst_cnt;
        ssize_t speed_check_delay;
        /* 80KB */
        ssize_t speed_check_bytes;
        /* 2000 MB */
        ssize_t speed_limit_rate;
        ssl_config_t ssl_config;
        bool tcp_no_delay;
        bool verify_peer;
        std::move_only_function<bool(const std::string &name)> function_contains_compressor_ = nullptr;
        std::string cipher_list;
    };
}