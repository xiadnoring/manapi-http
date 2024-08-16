#ifndef MANAPIHTTPPOOL_H
#define MANAPIHTTPPOOL_H

#include <ev++.h>
#include <netdb.h>
#include "ManapiUtils.h"
#include "ManapiThreadSafe.h"
#include "ManapiJson.h"
#include "ManapiTask.h"
#include "ManapiHttp.h"

#define MANAPI_QUIC_CONNECTION_ID_LEN 16
#define MANAPI_MAX_DATAGRAM_SIZE 1350
#define MANAPI_SEND_BURST_LIMIT 65507

namespace manapi::net {
    class http;

    constexpr static size_t quic_token_max_len = sizeof ("quiche") - 1 + sizeof (struct sockaddr_storage) + QUICHE_MAX_CONN_ID_LEN;

    struct ssl_config_t {
        bool            enabled = false;
        std::string     key;
        std::string     cert;
    };

    namespace versions {
        enum {
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

        enum {
            HTTP_v0_9 = 0,
            HTTP_v1_0 = 1,
            HTTP_v1_1 = 2,
            HTTP_v2 = 3,
            HTTP_v3 = 4
        };
    }

    struct http_quic_conn_io;

    typedef manapi::utils::safe_unordered_map <std::string, http_quic_conn_io *> QUIC_MAP_CONNS_T;

    struct http_quic_conn_io {
        int                 sock_fd;

        quiche_conn         *conn;
        quiche_h3_conn      *http3;

        sockaddr_storage    peer_addr;
        socklen_t           peer_addr_len;

        ev::timer           timer;

        std::string         key;

        std::mutex          mutex;
        std::mutex          wait;

        utils::VEC_STR      buffers;

        std::unordered_map  <uint64_t, task *> tasks;
        QUIC_MAP_CONNS_T*   conns;
    };


    class http_pool {
    public:
        explicit http_pool(const utils::json &config, http *server, const size_t &id);
        ~http_pool();

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

        void set_port (const std::string &_port);
        [[nodiscard]] const std::string& get_port () const;

        const ssl_config_t          &get_ssl_config ();
        ev::loop_ref                get_loop ();

        void                        stop ();
        std::future <int>           run ();

        void                        new_connection_quic    (ev::io &watcher, int revents);
        void                        new_connection_tls     (ev::io &watcher, int revents);

        http                        *get_http () const;

        // resolve
        sockaddr                    *server_addr = nullptr;
        socklen_t                   server_len;
        quiche_h3_config            *http3_config;
        // quic data
        quiche_config               *quic_config;
        QUIC_MAP_CONNS_T            quic_map_conns;

        utils::safe_unordered_map <utils::manapi_socket_information, task *> peer_by_ip;
        std::mutex                  recv_m;

        const int                   &get_fd ();

        // watchers
        ev::io                      *ev_io = nullptr;
    private:
        int                         _pool ();
        static SSL_CTX*             ssl_create_context (const size_t &version = versions::TLS_v1_3);
        void                        ssl_configure_context ();

        size_t                      id;

        // settings
        bool                        quic_debug              = false;
        size_t                      quic_cc_algo            = versions::QUIC_CC_RENO;
        size_t                      tls_version             = versions::TLS_v1_3;
        size_t                      max_header_block_size   = 4096;
        size_t                      socket_block_size       = 1350;
        size_t                      partial_data_min_size   = 4194304;
        size_t                      http_version            = versions::HTTP_v1_1;
        std::string                 http_version_str        = "1.1";
        std::string                 address                 = "0.0.0.0";
        std::string                 port                    = "8888";// settings
        std::string                 http_implement          = "tls";
        size_t                      keep_alive              = 2;
        std::string                 quic_implement          = "quiche";

        ssl_config_t                ssl_config;



        SSL_CTX                     *ctx;

        http                        *server;

        int                         sock_fd{},
                                    conn_fd{};

        // pool

        std::mutex                  m_running;
        std::mutex                  m_initing;

        ev::dynamic_loop            loop;
        addrinfo                    *local          = nullptr;
        std::promise <int>          *pool_promise   = nullptr;
    };
}

#endif //MANAPIHTTPPOOL_H
