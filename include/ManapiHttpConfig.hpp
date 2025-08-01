/**
 * @file ManapiHttpConfig.hpp
 * @brief Provides a config interface for Http Servers
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <string>
#include <functional>
#if defined(_WIN32)
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <winsock2.h>
#   include <ws2tcpip.h>
#elif defined(__unix__)||defined(__APPLE__)
#   include <sys/socket.h>
#endif

#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"
#include "async/ManapiAsyncConditionVariable.hpp"
#include "components/ManapiConfig.hpp"

namespace manapi::net::http {
    namespace versions {
        /**
         * it provides all existing TLS versions
         */
        enum tls {
            TLS_v1 = 1,
            TLS_v1_1,
            TLS_v1_2,
            TLS_v1_3,
            DTLS_v1,
            DTLS_v1_2,
            DTLS_v1_3
        };

        /**
         * it provides all existing QUIC algorithms
         */
        enum quic_cc {
            QUIC_CC_NONE = 1,
            QUIC_CC_RENO,
            QUIC_CC_CUBIC,
            QUIC_CC_BBR,
            QUIC_CC_BBR2
        };

        /**
         * it provides all existing HTTP versions
         */
        enum http {
            HTTP_v0_9 = 1,
            HTTP_v1_0,
            HTTP_v1_1,
            HTTP_v2,
            HTTP_v3
        };
    }

    /**
     * it helps to configure all http pools and endpoints
     */
    class config : public manapi::internal::config_interface {
    public:
        config (const json &config);

        ~config ();

        /**
         * Is the Http version allowed and supported
         *
         * @param version Http version
         * @return True if the passed Http version is allowed
         */
        bool contains_http_version (int version);

        std::vector<std::string_view> alpns ();

        /**
         * Is the compressor is availabled by the passed name
         *
         * @param name Compressor name (gzip, brotli, deflate and etc)
         * @return true if the compressor exists
         */

        [[nodiscard]] bool contains_compressor (const std::string &name);

        void function_contains_compressor (std::move_only_function<bool(const std::string &name)> func);

        /**
         * Returns Http version as a string
         * If http version doesn't exist it returns empty string
         *
         * @param version Http version
         * @return Http version as a string
         */
        static std::string_view stringify_http_version (int version);

        /**
         * Returns http version as an integer
         *
         * @param version
         * @return NotFound if http version invalid otherwise http version as an integer
         */
        static manapi::error::status_or<http::versions::http> parse_http_version (std::string_view version) MANAPIHTTP_NOEXCEPT;

        // settings
        int max_working_streams;

        /**
         * For Http/2 and QUIC
         * Max concurrent streams by the connection
         *
         * value '-1' means to use default value
         */
        int max_concurrent_streams;

        /**
         * For Http/2 and QUIC
         * Max frame size
         *
         * value '-1' means to use default value
         */
        int max_frame_size;

        /**
         * For Http/2 HPACK
         */
        int max_hpack_table_size;

        /**
         * For Http/2 HPACK
         */
        int max_hpack_list_size;

        /**
         * For Http/2 and QUIC
         * Initial window size by the stream
         */
        int initial_window_size;

        /**
         * The size of the stream window that will be supported
         * by the application
         */
        uint32_t window_stream_size;

        /**
         * The size of the connection window that will be supported
         * by the application
         */
        uint32_t window_connection_size;

        /**
         * The number of the buffers stack 'max_merge_buffer_stack' is required for
         * sending buffers to endpoints
         */
        int max_merge_buffer_stack;

        /**
         * Connects in advance if 'simultaneous_accepts' is true
         */
        bool simultaneous_accepts;

        /**
         * Max size of the header block
         * Works for Http/[1-3]
         */
        uint32_t max_headers_size;

        /**
         * Max size of the header key
         */
        uint16_t max_header_key_size;

        /**
         * Max size of the header value
         */
        uint16_t max_header_value_size;

        /**
         * Minimal requirements to use partitial methods
         */
        size_t partial_data_min_size;

        /**
         * Contains http version as a flag
         */
        uint32_t http_versions;

        /**
         * Host Address
         */
        std::string address;

        /**
         * Host Port
         */
        std::string port;// settings

        /**
         * Transport Implementation (default, openssl and etc)
         */
        std::string implementation;

        /**
         * Transport (udp, tcp, quic, tls)
         */
        std::string transport;

        /**
         * Max size of the buffer stack to store
         */
        size_t max_buffer_stack;

        /**
         * Keep Alive (for TCP connections and Http/1)
         */
        size_t keep_alive;

        /**
         * Contains server address in binary format
         */
        sockaddr_storage server_addr;

        /**
         * Size of the server address struct
         */
        socklen_t server_len;

        /**
         * Maximum number of connections to support
         * If the size of connections reaches the limit, the server sends an error page
         * If the size of connections reaches the double limit, the server doesn't accept new connections
         */
        size_t max_connections;

        /**
         * It's the same as 'max_connections'
         */
        size_t max_connections_by_ip;

        /**
         * Tcp backlog option
         */
        int tcp_backlog;

        /**
         * Max buffer size
         */
        ssize_t buffer_size;

        /**
         * max count of reset streams when connected
         */
        ssize_t max_rst_cnt;

        /**
         * Sets the speed check delay interval
         */
        ssize_t speed_check_delay;
        /**
         * Sets the minimum limit rate every 'speed_check_delay' seconds
         */
        ssize_t speed_check_bytes;

        /**
         * Sets the maximum limit rate every second
         */
        ssize_t speed_limit_rate;

        /**
         * Tcp no_delay option
         */
        bool tcp_no_delay;

        /**
         * Http/1 implementation
         */
        std::string http1_implementation;

        /**
         * Http/2 implementation
         */
        std::string http2_implementation;

        /**
         * Http/3 implementation
         */
        std::string http3_implementation;

        /**
         * Ssl options
         */
        manapi::json ssl;

        /**
         * QUIC options
         */
        manapi::json quic;

        bool force_conn_shutdown;

        std::move_only_function<bool(const std::string &name)> function_contains_compressor_ = nullptr;
    };
}
