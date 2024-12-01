#ifndef MANAPIHTTPCONFIG_HPP
#define MANAPIHTTPCONFIG_HPP

#include <string>
#include <functional>
#include <sys/socket.h>

#include "ManapiJson.hpp"

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
        [[nodiscard]] const size_t& get_socket_block_size () const;

        void set_max_header_block_size (const size_t &s);
        [[nodiscard]] const size_t& get_max_header_block_size () const;

        [[nodiscard]] const size_t& get_partial_data_min_size () const;

        void set_http_version (const size_t &new_http_version);
        [[nodiscard]] const size_t& get_http_version () const;

        void set_http_version_str (const std::string &new_http_version);
        [[nodiscard]] const std::string& get_http_version_str () const;

        void set_keep_alive (const long int &seconds);
        [[nodiscard]] const size_t& get_keep_alive () const;

        [[nodiscard]] const ssize_t& get_recv_timeout () const;
        [[nodiscard]] const ssize_t& get_send_timeout () const;

        void set_port (const std::string &_port);
        [[nodiscard]] const std::string& get_port () const;

        [[nodiscard]] const std::string& get_implementation () const;
        [[nodiscard]] const std::string& get_transport () const;
        [[nodiscard]] const std::string& get_address () const;
        [[nodiscard]] const std::string& get_quic_implement () const;

        [[nodiscard]] const size_t& get_tls_version () const;

        [[nodiscard]] const bool &is_quic_debug () const;

        [[nodiscard]] const size_t &get_quic_cc_algo () const;

        const ssl_config_t          &get_ssl_config ();

        void set_server_address (const sockaddr &addr);
        sockaddr &get_server_address ();
        void set_server_len (const size_t &len);
        [[nodiscard]] const socklen_t &get_server_len () const;

        // void set_http3_config (quiche_h3_config *config);
        // quiche_h3_config *get_http3_config ();
        //
        // void set_quic_config (quiche_config *config);
        // quiche_config *get_quic_config ();

        void set_socket_fd (const int &fd);
        [[nodiscard]] const int &get_socket_fd () const;

        bool contains_compressor (const std::string &name) const;
        void set_function_contains_compressor (const std::function<bool(const std::string &name)> &func);

        static const std::string &stringify_http_version (const versions::http &version);
        static http::versions::http parse_http_version (const std::string &version);
    private:
        // settings
        bool                        quic_debug              = false;
        size_t                      quic_cc_algo            = versions::QUIC_CC_RENO;
        size_t                      tls_version             = versions::TLS_v1_3;
        size_t                      max_header_block_size   = 4096;
        size_t                      socket_block_size       = 1350;
        size_t                      partial_data_min_size   = 0;
        size_t                      http_version            = versions::HTTP_v1_1;
        std::string                 http_version_str        = "1.1";
        std::string                 address                 = "0.0.0.0";
        std::string                 port                    = "8888";// settings
        std::string                 implementation          = "default";
        std::string                 transport               = "tcp";
        size_t                      keep_alive              = 2;
        sockaddr                    server_addr;
        socklen_t                   server_len;

        int                         sock_fd{};
        ssize_t                     recv_timeout            = 1000;
        ssize_t                     send_timeout            = 1000;
        ssl_config_t                ssl_config;
        std::function<bool(const std::string &name)> function_contains_compressor = nullptr;
    };
}

#endif //MANAPIHTTPCONFIG_HPP
