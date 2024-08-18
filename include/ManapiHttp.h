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
#include "ManapiJsonMask.h"
#include "ManapiApi.h"
#include "ManapiHttpPool.h"

#define MANAPI_NET_TYPE_TCP 0
#define MANAPI_NET_TYPE_UDP 1

#define MANAPI_HTTP_RESP_TEXT 0
#define MANAPI_HTTP_RESP_FILE 1
#define MANAPI_HTTP_RESP_PROXY 2

#define REQ(_x) manapi::net::http_request &_x
#define RESP(_x) manapi::net::http_response &_x

#define ASYNC(_x) resp.tasks->async([&] () {_x;})
#define AWAIT(_x) resp.tasks->await([&] () {_x;})

#define ASYNC2(_y, _x) _y.tasks->async([&] () {_x;})
#define AWAIT2(_y, _x) _y.tasks->await([&] () {_x;})

#define HANDLER(_req, _resp) (REQ(_req), RESP(_resp))

namespace manapi::net {
    class http_pool;
    class http_response;
    class http_request;

    typedef std::function <void(manapi::net::http_request &req, manapi::net::http_response &res)> handler_template_t;

    struct http_uri_part;

    typedef std::map<std::string, http_uri_part *>              handlers_map_t;
    typedef std::pair<std::regex, http_uri_part *>              handlers_regex_pair_t;
    typedef std::map<std::string, handlers_regex_pair_t>        handlers_regex_map_t;
    typedef std::vector<std::string>                            handlers_regex_titles_t;
    typedef std::map <std::string, std::string>                 handlers_static_types_t;

    struct http_handler_functions {
        handler_template_t  handler = nullptr;

        const utils::json_mask  *post_mask = nullptr;
        const utils::json_mask  *get_mask = nullptr;
    };

    typedef std::map <std::string, http_handler_functions>      handlers_types_t;

    struct http_handler_page {
        const http_handler_functions    *handler = nullptr;
        const http_handler_functions    *error = nullptr;
        std::string                     *statics = nullptr;
        size_t                          statics_parts_len{};
    };

    struct http_uri_part {
        handlers_map_t          *map            = nullptr;

        // params
        handlers_regex_titles_t *params         = nullptr;

        // functions (handler, post_mask, get_mask) by method
        handlers_types_t        *handlers        = nullptr;
        // errors
        handlers_types_t        *errors         = nullptr;
        // static handler functions
        handlers_static_types_t *statics        = nullptr;
        // regex handler functions
        handlers_regex_map_t    *regexes        = nullptr;

        bool                    has_body        = false;
    };

    class http {
    public:
        http();
        ~http();
        std::future <void>      pool (const size_t &thread_num = 20);

        void GET    (const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask = nullptr);
        void POST   (const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask = nullptr, const utils::json_mask &post_mask = nullptr);
        void PUT    (const std::string &uri, const handler_template_t &handler);
        void DELETE (const std::string &uri, const handler_template_t &handler);
        void PATCH  (const std::string &uri, const handler_template_t &handler);

        void GET    (const std::string &uri, const std::string &folder);

        http_uri_part               *set_handler (const std::string &method, const std::string &uri, const handler_template_t &handler, bool has_body = false, const utils::json_mask &get_mask = nullptr, const utils::json_mask &post_mask = nullptr);
        http_uri_part               *set_handler (const std::string &method, const std::string &uri, const std::string &folder, bool has_body = false);

        http_handler_page           get_handler (request_data_t &request_data) const;

        void set_compressor (const std::string &name, manapi::utils::compress::TEMPLATE_INTERFACE handler);
        manapi::utils::compress::TEMPLATE_INTERFACE get_compressor (const std::string &name);

        bool                        contains_compressor (const std::string &name) const;

        threadpool<task>            *get_tasks_pool () const;

        void                        set_config (const std::string &path);
        const manapi::utils::json   &get_config ();

        const std::string           *get_compressed_cache_file (const std::string &file, const std::string &algorithm) const;
        void                        set_compressed_cache_file (const std::string &file, const std::string &compressed, const std::string &algorithm);

        // config
        std::string                 *config_cache_dir = nullptr;

        void                        stop (bool wait = false);

        static std::vector <http *>
                                    running;
        static bool                 stopped_interrupt;

        void                        append_task (task *t, bool important = false);
    private:
        static void                 destroy_uri_part (http_uri_part *cur);
        http_uri_part               *build_uri_part (const std::string &uri, size_t &type);

        void                        setup ();
        void                        setup_config ();
        void                        save ();
        void                        save_config ();

        void                        stop_pool ();

        manapi::utils::json         config;
        manapi::utils::json         cache_config;

        std::string                 config_path = "/tmp/http.json";

        threadpool<task>            *tasks_pool = nullptr;

        http_uri_part               handlers;


        bool                        enabled_save_config     = false;

        std::map <std::string, manapi::utils::compress::TEMPLATE_INTERFACE> compressors;

        static std::string          default_cache_dir;
        static std::string          default_config_name;

        std::mutex                  m_initing;
        std::mutex                  m_running;
        std::mutex                  m_stopping;
        std::condition_variable     cv_stopping;
        std::atomic <bool>          stopping;

        std::unique_ptr<std::promise <void>> pool_promise;

        std::unordered_map<size_t, http_pool *> pools;

        size_t                      next_pool_id = 0;
    };
}

#endif //MANAPIHTTP_MANAPIHTTP_H
