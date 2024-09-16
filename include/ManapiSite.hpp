#ifndef MANAPISITE_HPP
#define MANAPISITE_HPP

#include <chrono>
#include <regex>
#include <quiche.h>
#include <list>

#include "ManapiJson.hpp"
#include "ManapiJsonMask.hpp"
#include "ManapiTask.hpp"
#include "ManapiTimerPool.hpp"
#include "ManapiCompress.hpp"
#include "ManapiThreadSafe.hpp"

#include "ManapiHttpRequest.hpp"
#include "ManapiHttpResponse.hpp"

#define MANAPI_QUIC_CONNECTION_ID_LEN 16
#define MANAPI_MAX_DATAGRAM_SIZE 1350
#define MANAPI_SEND_BURST_LIMIT 65507
#define MANAPI_QUIC_CAPACITY_MIN 5

namespace manapi::net {
    typedef std::function <void(manapi::net::http_request &req, manapi::net::http_response &res)> handler_template_t;

    struct http_quic_conn_io {
        int                     sock_fd;
        quiche_conn             *conn;
        quiche_h3_conn          *http3;
        sockaddr_storage        peer_addr;
        socklen_t               peer_addr_len;
        size_t                  timer_id;
        std::string             key;
        bool                    is_deleting = false;
        bool                    is_responsing = false;
        bool                    is_pooling = false;
        std::list<std::string>  buffers;
        std::mutex              buffers_mutex;
        std::mutex              mutex;

        std::unordered_map  <uint64_t, task *> tasks;
    };

    typedef manapi::utils::safe_unordered_map <std::string, http_quic_conn_io *> QUIC_MAP_CONNS_T;

    struct http_uri_part;

    struct http_handler_functions {
        handler_template_t handler = nullptr;

        std::unique_ptr<const utils::json_mask> post_mask = nullptr;
        std::unique_ptr<const utils::json_mask> get_mask = nullptr;
    };

    typedef std::map<std::string, std::unique_ptr<http_uri_part>>   handlers_map_t;
    typedef std::pair<std::regex, std::unique_ptr<http_uri_part>>   handlers_regex_pair_t;
    typedef std::map<std::string, handlers_regex_pair_t>            handlers_regex_map_t;
    typedef std::vector<std::string>                                handlers_regex_titles_t;
    typedef std::map <std::string, std::string>                     handlers_static_types_t;
    typedef std::map <std::string, http_handler_functions>          handlers_types_t;


    struct http_handler_page {
        const http_handler_functions                                *handler = nullptr;
        const http_handler_functions                                *error = nullptr;
        std::vector<const http_handler_functions*>                  layer;
        std::string                                                 *statics = nullptr;
        size_t                                                      statics_parts_len{};
    };

    struct http_uri_part {
        std::unique_ptr<handlers_map_t>             map = nullptr;

        // params
        std::unique_ptr<handlers_regex_titles_t>    params = nullptr;

        // functions (handler, post_mask, get_mask) by method
        std::unique_ptr<handlers_types_t>           handlers = nullptr;
        // errors
        std::unique_ptr<handlers_types_t>           errors = nullptr;
        // layers
        std::unique_ptr<handlers_types_t>           layers = nullptr;
        // static handler functions
        std::unique_ptr<handlers_static_types_t>    statics = nullptr;
        // regex handler functions
        std::unique_ptr<handlers_regex_map_t>       regexes = nullptr;
    };

    class site {
    public:
        site ();
        ~site();

        void                                append_task (task *t, const int &level = 0);
        size_t                              append_timer (const std::chrono::milliseconds &m, const std::function<void()> &t);
        void                                remove_timer (const size_t &id);

        http_uri_part                       *set_handler (const std::string &method, const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask = nullptr, const utils::json_mask &post_mask = nullptr);
        http_uri_part                       *set_handler (const std::string &method, const std::string &uri, const std::string &folder);

        http_handler_page                   get_handler (request_data_t &request_data) const;

        void set_compressor (const std::string &name, manapi::utils::compress::TEMPLATE_INTERFACE handler);
        manapi::utils::compress::TEMPLATE_INTERFACE get_compressor (const std::string &name);

        bool                                contains_compressor (const std::string &name) const;

        void                                set_config (const std::string &path);
        void                                set_config_object (const utils::json &config);
        const manapi::utils::json           &get_config ();

        const std::string                   *get_compressed_cache_file (const std::string &file, const std::string &algorithm) const;
        void                                set_compressed_cache_file (const std::string &file, const std::string &compressed, const std::string &algorithm);

        const std::unique_ptr<manapi::net::threadpool<manapi::net::task>> &get_tasks_pool () const;
        void                                tasks_pool_stop ();
        void                                tasks_pool_init (const size_t &thread_num);

        std::string                         *config_cache_dir = nullptr;
    protected:
        void                                setup ();
        void                                timer_pool_setup (threadpool<task> *tasks_pool);
        void                                timer_pool_stop ();
        void                                setup_config ();
        void                                save ();
        void                                save_config ();
        manapi::utils::json                 config;
    private:
        static void                         check_exists_method_on_url (const std::string &url, const std::unique_ptr<handlers_types_t> &m, const std::string &method);
        static void                         check_exists_method_on_url (const std::string &url, const std::unique_ptr<handlers_static_types_t> &m, const std::string &method);
        std::unique_ptr<threadpool<task>>   tasks_pool = nullptr;
        http_uri_part                       *build_uri_part (const std::string &uri, size_t &type);
        std::unique_ptr<utils::timerpool>   timerpool;

        manapi::utils::json                 cache_config;

        std::string                         config_path = "/tmp/http.json";
        bool                                enabled_save_config     = false;

        http_uri_part                       handlers;

        std::map <std::string, manapi::utils::compress::TEMPLATE_INTERFACE> compressors;

        static std::string                  default_cache_dir;
        static std::string                  default_config_name;
        // config

    };
}

#endif //MANAPISITE_HPP
