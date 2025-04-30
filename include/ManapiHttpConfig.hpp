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
        config (async::shared_ctx ctx, const json &config);
        ~config ();

        void max_header_block_size (const size_t &s);
        [[nodiscard]] std::atomic<size_t> &max_header_block_size ();

        [[nodiscard]] std::atomic<size_t> &partial_data_min_size ();

        bool contains_http_version (int version);
        int recommended_http_version ();
        Atomic<std::set<int>> &http_versions ();

        void keep_alive (const long int &seconds);
        [[nodiscard]] std::atomic<size_t> &keep_alive ();

        std::atomic<size_t> &max_connections ();
        std::atomic<int> &max_backlog();

        void port (const std::string &_port);
        [[nodiscard]] AtomicReference<std::string> port ();

        [[nodiscard]] AtomicReference<std::string> implementation ();
        [[nodiscard]] AtomicReference<std::string> transport ();
        [[nodiscard]] AtomicReference<std::string> address ();
        [[nodiscard]] AtomicReference<std::string> quic_implement ();
        [[nodiscard]] AtomicReference<std::string> cipher_list();

        [[nodiscard]] std::atomic<size_t> &tls_version ();

        [[nodiscard]] std::atomic<bool> &is_quic_debug ();

        [[nodiscard]] std::atomic<size_t> &quic_cc_algo ();

        std::atomic<ssize_t> &max_rst_cnt ();
        std::atomic<ssize_t> &speed_check_delay ();
        std::atomic<ssize_t> &speed_check_bytes ();
        std::atomic<ssize_t> &speed_limit_rate ();

        AtomicReference<ssl_config_t> ssl_config ();

        void server_address (const sockaddr &addr);
        AtomicReference<sockaddr> server_address ();
        void server_len (const size_t &len);
        [[nodiscard]] std::atomic<socklen_t> &server_len ();

        [[nodiscard]] bool contains_compressor (const std::string &name);
        void function_contains_compressor (std::move_only_function<bool(const std::string &name)> func);

        static const std::string &stringify_http_version (const int &version);
        static http::versions::http parse_http_version (const std::string &version);

        std::atomic<bool> &tcp_no_delay ();

        std::atomic<bool> &verify_peer ();

        std::atomic<ssize_t> &buffer_size ();
    private:
        // settings
        std::atomic<bool> quic_debug_;
        std::atomic<size_t> quic_cc_algo_;
        std::atomic<size_t> tls_version_;
        std::atomic<size_t> max_header_block_size_;
        std::atomic<size_t> partial_data_min_size_;
        Atomic<std::set<int>> http_versions_ = {};
        Atomic<std::string> address_;
        Atomic<std::string> port_;// settings
        Atomic<std::string> implementation_;
        Atomic<std::string> transport_;
        std::atomic<size_t> keep_alive_;
        Atomic<sockaddr> server_addr_;
        std::atomic<socklen_t> server_len_;
        std::atomic<size_t> max_plain_param_length_  = 16000UL;
        std::atomic<size_t> max_file_param_length_   = 2147483648UL;
        std::atomic<size_t> max_connections_;
        std::atomic<int> max_backlog_;
        std::atomic<ssize_t> buffer_size_;
        std::atomic<ssize_t> max_rst_cnt_;
        std::atomic<ssize_t> speed_check_delay_;
        /* 80KB */
        std::atomic<ssize_t> speed_check_bytes_;
        /* 2000 MB */
        std::atomic<ssize_t> speed_limit_rate_;
        Atomic<ssl_config_t> ssl_config_;
        std::atomic<bool> tcp_no_delay_;
        std::atomic<bool> verify_peer_;
        std::move_only_function<bool(const std::string &name)> function_contains_compressor_ = nullptr;
        Atomic<std::string> cipher_list_;
    };
}