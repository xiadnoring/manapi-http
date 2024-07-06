#ifndef MANAPIHTTP_MANAPIHTTP_H
#define MANAPIHTTP_MANAPIHTTP_H

#include <netinet/in.h>
#include <functional>
#include <map>
#include <regex>
#include <atomic>
#include <thread>
#include <quiche.h>
#include <openssl/ssl.h>
#include <future>
#include <ev++.h>
#include "ManapiHttpResponse.h"
#include "ManapiHttpRequest.h"
#include "ManapiThreadPool.h"
#include "ManapiThreadSafe.h"
#include "ManapiTask.h"
#include "ManapiCompress.h"
#include "ManapiJson.h"

#define MANAPI_NET_TYPE_TCP 0
#define MANAPI_NET_TYPE_UDP 1

#define REQ(_x) manapi::net::http_request &_x
#define RESP(_x) manapi::net::http_response &_x

#define HANDLER(_req, _resp) (REQ(_req), RESP(_resp))

namespace manapi::net {
    class http_response;
    class http_request;

    const static size_t quic_token_max_len = sizeof ("quiche") - 1 + sizeof (struct sockaddr_storage) + QUICHE_MAX_CONN_ID_LEN;

    typedef std::function <void(manapi::net::http_request &req, manapi::net::http_response &res)> handler_template_t;

    struct http_uri_part;

    typedef std::map<std::string, http_uri_part *>              handlers_map_t;
    typedef std::pair<std::regex, http_uri_part *>              handlers_regex_pair_t;
    typedef std::map<std::string, handlers_regex_pair_t>        handlers_regex_map_t;
    typedef std::vector<std::string>                            handlers_regex_titles_t;
    typedef std::map <std::string, handler_template_t>          handlers_types_t;
    typedef std::map <std::string, std::string>                 handlers_static_types_t;

    struct http_handler_page {
        handlers_types_t    errors;
        handler_template_t  handler = nullptr;
        std::string         *statics = nullptr;
        size_t              statics_parts_len;
    };

    struct http_qc_conn_io {
        int                 sock_fd;

        uint8_t             cid[quic_token_max_len];

        quiche_conn         *conn;
        quiche_h3_conn      *http3;

        struct              sockaddr_storage peer_addr;
        socklen_t           peer_addr_len;

        ev::timer           timer;

        std::string         key;

        std::mutex          mutex;

        std::unordered_map  <uint64_t, task *> tasks;
    };

    struct http_uri_part {
        handlers_map_t          *map            = nullptr;

        // params
        handlers_regex_titles_t *params         = nullptr;

        // just handler functions
        handlers_types_t        *handlers       = nullptr;
        // error handler functions
        handlers_types_t        *errors         = nullptr;
        // static handler functions
        handlers_static_types_t *statics        = nullptr;
        // regex handler functions
        handlers_regex_map_t    *regexes        = nullptr;

        bool                    has_body        = false;
    };

    struct ssl_config_t {
        bool            enabled = false;
        std::string     key;
        std::string     cert;
    };

    typedef manapi::utils::safe_unordered_map <std::string, http_qc_conn_io *> QUIC_MAP_CONNS_T;

    class http {
    public:
        explicit http(std::string port = "8888");
        int pool (const size_t &thread_num = 20);
        std::future <int> run ();

        void GET    (const std::string &uri, const handler_template_t &handler);
        void POST   (const std::string &uri, const handler_template_t &handler);
        void PUT    (const std::string &uri, const handler_template_t &handler);
        void DELETE (const std::string &uri, const handler_template_t &handler);
        void PATCH  (const std::string &uri, const handler_template_t &handler);

        void GET    (const std::string &uri, const std::string &folder);

        http_uri_part       *set_handler (const std::string &method, const std::string &uri, const handler_template_t &handler, bool has_body = false);
        http_uri_part       *set_handler (const std::string &method, const std::string &uri, const std::string &folder, bool has_body = false);

        http_handler_page get_handler (request_data_t &request_data);

        void set_keep_alive (const long int &seconds);
        [[nodiscard]] const long int& get_keep_alive () const;

        void set_socket_block_size (const size_t &s);
        [[nodiscard]] const size_t& get_socket_block_size () const;

        void set_max_header_block_size (const size_t &s);
        [[nodiscard]] const size_t& get_max_header_block_size () const;

        [[nodiscard]] const size_t& get_partial_data_min_size () const;

        void set_http_version (const size_t &new_http_version);
        [[nodiscard]] const size_t& get_http_version () const;

        void set_http_version_str (const std::string &new_http_version);
        [[nodiscard]] const std::string& get_http_version_str () const;

        void set_compressor (const std::string &name, manapi::utils::compress::TEMPLATE_INTERFACE handler);
        manapi::utils::compress::TEMPLATE_INTERFACE get_compressor (const std::string &name);

        bool contains_compressor (const std::string &name);

        void set_port (const std::string &_port);

        void set_config (const std::string &path);
        const manapi::utils::json & get_config ();

        const std::string *get_compressed_cache_file (const std::string &file, const std::string &algorithm);
        void set_compressed_cache_file (const std::string &file, const std::string &compressed, const std::string &algorithm);

        const ssl_config_t &get_ssl_config ();

        // config
        std::string *config_cache_dir = nullptr;

        void stop ();
        ev::loop_ref get_loop ();

        struct sockaddr *server_addr;
        socklen_t       server_len;

        quiche_h3_config *http3_config;
    private:
        http_uri_part *build_uri_part (const std::string &uri, size_t &type);

        static SSL_CTX* ssl_create_context ();
        void            ssl_configure_context ();

        void setup ();
        void setup_config ();
        void save ();
        void save_config ();

        void new_connection_udp    (ev::io &watcher, int revents);
        void new_connection_tls     (ev::io &watcher, int revents);

        manapi::utils::json config;
        manapi::utils::json cache_config;

        std::string         config_path = "/tmp/http.json";

        int                 sock_fd,
                            conn_fd{};

        threadpool<task>    *tasks_pool = nullptr;

        http_uri_part       handlers;

        // settings
        long int            keep_alive;

        size_t              max_header_block_size   = 4096;
        size_t              socket_block_size       = 1350;
        size_t              partial_data_min_size   = 4194304;
        size_t              http_version            = 1;
        std::string         http_version_str        = "1.1";
        std::string         address                 = "0.0.0.0";
        std::string         port                    = "8888";
        bool                enabled_save_config     = false;

        ssl_config_t        ssl_config;

        ev::dynamic_loop    loop;

        std::map <std::string, manapi::utils::compress::TEMPLATE_INTERFACE> compressors;

        static std::string  default_cache_dir;
        static std::string  default_config_name;

        // quic data
        quiche_config       *q_config;
        QUIC_MAP_CONNS_T    quic_map_conns;

        // watchers
        ev::io              *tcp_io = nullptr;
        ev::io              *udp_io = nullptr;

        SSL_CTX             *ctx;
    };
}

#endif //MANAPIHTTP_MANAPIHTTP_H
