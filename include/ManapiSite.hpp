#pragma once

#include <chrono>
#include <ev++.h>
#include <regex>
#include <list>
#include <set>

#include "ManapiAsync.hpp"
#include "ManapiJson.hpp"
#include "ManapiJsonMask.hpp"
#include "services/ManapiTask.hpp"
#include "services/ManapiTimerPool.hpp"
#include "compress/ManapiCompress.hpp"
#include "async/ManapiAsyncTimer.hpp"

#include "ManapiHttpRequest.hpp"
#include "ManapiHttpResponse.hpp"
#include "async/ManapiAsyncMutex.hpp"
#include "services/ManapiEventLoop.hpp"

namespace manapi::net::worker {
    class base;
}

namespace manapi::net {
    typedef std::function <future<void>(manapi::net::http_request &req, manapi::net::http_response &res)> handler_template_t;

    struct http_uri_part;

    struct http_handler_functions {
        handler_template_t handler = nullptr;

        std::unique_ptr<const json_mask> post_mask = nullptr;
        std::unique_ptr<const json_mask> get_mask = nullptr;
    };

    typedef std::map<std::string, std::unique_ptr<http_uri_part>>   handlers_map_t;
    typedef std::pair<std::regex, std::unique_ptr<http_uri_part>>   handlers_regex_pair_t;
    typedef std::map<std::string, handlers_regex_pair_t>            handlers_regex_map_t;
    typedef std::vector<std::string>                                handlers_regex_titles_t;
    typedef std::map <std::string, std::string>                     handlers_static_types_t;
    typedef std::map <std::string, http_handler_functions>          handlers_types_t;


    struct http_handler_page {
        const http_handler_functions                                *handler = nullptr;
        std::unique_ptr<http_handler_page>                          error = nullptr;
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
        site (const std::shared_ptr<async::context> &ctx);
        virtual ~site();

        http_uri_part *set_handler (const std::string &method, const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        http_uri_part *set_handler (const std::string &method, const std::string &uri, const std::string &folder);

        http_handler_page get_handler (http::request_data_t &request_data) const;

        void set_compressor (const std::string &name, const std::function<future<bool>(const std::string &src, const std::string &dest)> &handler);
        const std::function<future<bool>(const std::string &src, const std::string &dest)> &get_compressor (const std::string &name);

        [[nodiscard]] bool contains_compressor (const std::string &name) const;

        void set_transport_protocol_worker (const std::string &type, const std::string &name, const std::function<std::shared_ptr<class worker::base>(net::site &site, std::shared_ptr<http::config> config)> &worker);
        const std::map <std::string, std::function<std::shared_ptr<manapi::net::worker::base>(std::shared_ptr<manapi::net::http::config> config)>> &get_transport_protocol_worker (const std::string &type);

        void set_config (const std::string &path);
        void set_config_object (const json &config);
        const manapi::json &get_config ();

        std::string get_compressed_cache_file (const std::string &file, const std::string &algorithm);
        void set_compressed_cache_file (const std::string &file, const std::string &compressed, const std::string &algorithm);

        [[nodiscard]] const std::shared_ptr<async::context>& async_context ();

        std::string config_cache_dir;
        std::shared_ptr<async::context> ctx;
        async::mutex cache_config_mx;
    protected:
        void setup ();
        void setup_config ();
        void save ();
        void save_config ();

        manapi::json config;
        std::mutex loopmx;
    private:
        static void check_exists_method_on_url (const std::string &url, const std::unique_ptr<handlers_types_t> &m, const std::string &method);
        static void check_exists_method_on_url (const std::string &url, const std::unique_ptr<handlers_static_types_t> &m, const std::string &method);
        http_uri_part *build_uri_part (const std::string &uri, size_t &type);

        manapi::json cache_config;
        std::string config_path = "/tmp/http.json";
        bool enabled_save_config     = false;

        http_uri_part handlers;


        std::map <std::string, std::function<future<bool>(const std::string &src, const std::string &dest)>> compressors;
        std::map <std::string, std::map <std::string, std::function<std::shared_ptr<worker::base>(std::shared_ptr<http::config> config)>>> transport_protocol_workers;

        static std::string default_cache_dir;
        static std::string default_config_name;
    };
}