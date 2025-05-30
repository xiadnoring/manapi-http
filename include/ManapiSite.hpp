#pragma once

#include <chrono>
#include <regex>
#include <list>
#include <set>

#include "ManapiUtils.hpp"
#include "ManapiAsync.hpp"
#include "ManapiJson.hpp"
#include "ManapiJsonMask.hpp"
#include "compress/ManapiCompress.hpp"

#include "async/ManapiAsyncMutex.hpp"
#include "http/Utils.hpp"
#include "ManapiHttpConfig.hpp"
#include "http/ManapiSiteCtx.hpp"

namespace manapi::net::worker {
    class base;
}

namespace manapi::net::http {
    class request;
    class response;
}

namespace manapi::net::http {
    typedef std::move_only_function <future<void>(manapi::net::http::request &req, manapi::net::http::response &res)> handler_template_t;

    struct http_uri_part;

    struct http_handler_function {
        handler_template_t handler = nullptr;

        std::unique_ptr<const json_mask> post_mask = nullptr;
        std::unique_ptr<const json_mask> get_mask = nullptr;
    };

    struct http_static_handler_function {
        std::string folder;
        std::unique_ptr<http_handler_function> layer;
    };

    typedef std::map<std::string, std::unique_ptr<http_uri_part>>   handlers_map_t;
    typedef std::pair<std::regex, std::unique_ptr<http_uri_part>>   handlers_regex_pair_t;
    typedef std::map<std::string, handlers_regex_pair_t>            handlers_regex_map_t;
    typedef std::vector<std::string>                                handlers_regex_titles_t;
    typedef std::map <std::string, http_static_handler_function>    handlers_static_types_t;
    typedef std::map <std::string, http_handler_function>           handlers_types_t;


    struct http_handler_page {
        http_handler_function                                       *handler = nullptr;
        std::unique_ptr<http_handler_page>                          error = nullptr;
        std::vector<http_handler_function*>                         layer;
        http_static_handler_function                                *statics = nullptr;
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
        typedef std::function<std::shared_ptr<worker::base>(site site, std::shared_ptr<manapi::net::worker::worker_config_t> wdata, std::shared_ptr<http::config> config)> implement_create_cb;
    protected:
        struct data_t {
            std::unique_ptr<async::condition_variable> cache_cv;
            ev::shared_async server_config_notifier;
            std::shared_ptr<worker::server_config_t> server_config;
            manapi::json cache_config;
            manapi::json config_;
            size_t cache_time;
            size_t config_time;
            std::string config_path;
            std::string config_cache_dir;
            server_ctx sctx;
            bool enabled_save_config;
            http_uri_part handlers;
            std::map <std::string, std::move_only_function<future<void>(std::string src, std::string dest)>> compressors_for_file{};
            std::map <std::string, std::move_only_function<std::string(std::string_view data)>> compressors_for_string{};
            std::map <std::string, std::map <std::string, implement_create_cb>> transport_protocol_workers{};
            std::mutex loopmx{};
        };
    public:
        site (server_ctx sctx);
        virtual ~site();

        site (site &&n) noexcept;
        site &operator=(site &&n) noexcept;
        site (const site &n);
        site &operator=(const site &n);

        http_uri_part *handler (std::string method, std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);
        http_uri_part *handler (std::string method, std::string uri, std::string folder, handler_template_t handler = nullptr, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        std::unique_ptr<http_handler_page> handler (http::request_data_t *request_data) const;

        void compressor_for_file (const std::string &name, std::move_only_function<future<void>(std::string src, std::string dest)> handler);
        void compressor_for_string (const std::string &name, std::move_only_function<std::string(std::string_view data)> handler);

        std::move_only_function<future<void>(std::string src, std::string dest)> &compressor_for_file (const std::string &name);
        std::move_only_function<std::string(std::string_view)> &compressor_for_string (const std::string &name);

        [[nodiscard]] bool contains_compressor_for_file (const std::string &name) const;
        [[nodiscard]] bool contains_compressor_for_string (const std::string &name) const;

        void transport_protocol_worker (const std::string &type, const std::string &name, implement_create_cb worker);
        const std::map <std::string, implement_create_cb> &transport_protocol_worker (const std::string &type);

        manapi::future<> config (std::string path);
        manapi::future<> config_object (json config);
        // const manapi::json &config ();

        manapi::future<std::pair<int, std::string>> get_compressed_cache_file (std::string file, std::string algorithm, std::chrono::system_clock::time_point filetime);
        manapi::future<> set_compressed_cache_file (std::string file, std::string compressed, std::string algorithm, std::chrono::system_clock::time_point filetime);

        [[nodiscard]] const std::string &config_cache_dir();

    protected:
        void setup ();
        manapi::future<> setup_config ();
        static manapi::future<> save_config (std::shared_ptr<data_t> data);

        std::shared_ptr<data_t> data;
        static std::string default_config_name;
    private:
        static http_handler_function default_error_handler;
        static void check_exists_method_on_url (const std::string &url, const std::unique_ptr<handlers_types_t> &m, const std::string &method);
        static void check_exists_method_on_url (const std::string &url, const std::unique_ptr<handlers_static_types_t> &m, const std::string &method);
        http_uri_part *build_uri_part (const std::string &uri, size_t &type);

    };
}