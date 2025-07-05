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

namespace manapi::net::http {
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

        [[nodiscard]] bool contains_compressor (const std::string &name);

        void function_contains_compressor (std::move_only_function<bool(const std::string &name)> func);

        static std::string_view stringify_http_version (int version);

        static http::versions::http parse_http_version (const std::string &version);

        template<typename T>
        static std::optional<T> get_value_config_param (const manapi::json &n) {
            return {};
        }

        template<typename T>
        static T get_config_param (const manapi::json &config, const std::string &name, T value) {
            auto &obb = config.as_object();
            auto it = obb.find(name);
            if (it != obb.end()) {
                auto res = get_value_config_param<T>(it->second);
                if (res.has_value())
                    return std::move(res.value());
            }
            return value;
        }

        // settings
        int max_working_streams;
        int max_concurrent_streams;
        int max_frame_size;
        int max_hpack_table_size;
        int max_hpack_list_size;
        int initial_window_size;
        uint32_t window_stream_size;
        uint32_t window_connection_size;
        int max_merge_buffer_stack;
        bool simultaneous_accepts;
        uint32_t max_headers_size;
        uint16_t max_header_key_size;
        uint16_t max_header_value_size;
        size_t partial_data_min_size;
        uint32_t http_versions;
        std::string address;
        std::string port;// settings
        std::string implementation;
        std::string transport;
        size_t max_buffer_stack;
        size_t keep_alive;
        sockaddr_storage server_addr;
        socklen_t server_len;
        size_t max_connections;
        int max_backlog;
        ssize_t buffer_size;
        ssize_t max_rst_cnt;
        ssize_t speed_check_delay;
        /* 80KB */
        ssize_t speed_check_bytes;
        /* 2000 MB */
        ssize_t speed_limit_rate;
        bool tcp_no_delay;
        std::string http1_implementation;
        std::string http2_implementation;
        std::string http3_implementation;
        manapi::json ssl;
        manapi::json quic;


        std::move_only_function<bool(const std::string &name)> function_contains_compressor_ = nullptr;
    };
}