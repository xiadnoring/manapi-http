/**
 * @file ManapiSite.hpp
 * @brief Provides the site structs
 *
 * @author Timur Zajnullin
 */

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
#include "http/ManapiHttpUtils.hpp"
#include "ManapiHttpConfig.hpp"
#include "http/ManapiSiteCtx.hpp"

namespace manapi::net::worker {
    class base;
    struct wrk_interface_global_t;
}

namespace manapi::net::http {
    class request;
    class response;
}

namespace manapi::net::http {
    typedef std::move_only_function <future<void>(manapi::net::http::request &req, manapi::net::http::response &res)> handler_template_t;

    struct http_handler_function;

    struct http_uri_part;

    struct http_static_handler_function;

    struct http_handler_page;

    typedef std::map<std::string, std::unique_ptr<http_uri_part>>   handlers_map_t;

    typedef std::pair<std::regex, std::unique_ptr<http_uri_part>>   handlers_regex_pair_t;

    typedef std::map<std::string, handlers_regex_pair_t>            handlers_regex_map_t;

    typedef std::vector<std::string>                                handlers_regex_titles_t;

    typedef std::map <std::string, http_static_handler_function>    handlers_static_types_t;

    typedef std::map <std::string, http_handler_function>           handlers_types_t;


    class site {
    public:
        /**
         * Compress file callback
         */
        typedef std::move_only_function<future<manapi::error::status>(std::string src, std::string dest)> compress_file_cb_t;

        /**
         * Compress string callback
         */
        typedef std::move_only_function<manapi::error::status_or<std::string>(std::string_view data)> compress_str_cb_t;

        typedef std::function<std::shared_ptr<worker::base>(site site, std::shared_ptr<multithread_storage::worker_t> wdata, std::shared_ptr<http::config> config)> implement_create_cb;
        typedef std::function<manapi::error::status_or<std::unique_ptr<worker::wrk_interface_global_t>> (worker::base *w)> implemenet_http_cb;
    protected:
        struct data_t;
    public:
        /**
         * initialize the site instance
         * @param sctx
         */
        site (server_ctx sctx);

        /**
         * deconstructor
         */
        virtual ~site();

        site (site &&n) noexcept;

        /* move */
        site &operator=(site &&n) noexcept;

        /* copy */
        site (const site &n);

        /* move */
        site &operator=(const site &n);

        /**
         * Set a handler by its method and URI
         * @param method method
         * @param uri URI path
         * @param handler handler
         * @param get_mask GET mask
         * @param post_mask POST mask
         * @return
         */
        http_uri_part *handler (std::string method, std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        /**
         * Set a folder sharing handler by its method and URI
         * @param method method
         * @param uri URI
         * @param folder folder
         * @param handler callback
         * @param get_mask GET mask
         * @param post_mask POST mask
         * @return
         */
        http_uri_part *handler (std::string method, std::string uri, std::string folder, handler_template_t handler = nullptr, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        /**
         * Get the handler by its method and URI
         * @param request_data the request data which contains the method and URI
         * @return
         */
        std::unique_ptr<http_handler_page> handler (http::request_data_t *request_data) const;

        /**
         * Add a compressor for files
         * @param name Algo Name
         * @param handler Callback
         *
         */
        void compressor_for_file (const std::string &name, compress_file_cb_t handler);

        /**
         * Add a compressor for plain texts
         * @param name Algo Name
         * @param handler Callback
         */
        void compressor_for_string (const std::string &name, compress_str_cb_t handler);

        compress_file_cb_t &compressor_for_file (const std::string &name);

        compress_str_cb_t &compressor_for_string (const std::string &name);

        [[nodiscard]] bool contains_compressor_for_file (const std::string &name) const;

        [[nodiscard]] bool contains_compressor_for_string (const std::string &name) const;

        /**
         * Add a transport protocol worker
         *
         * @param type Protocol Type
         * @param name Protocol Name
         * @param worker Worker Callback
         */
        void transport_protocol_worker (const std::string &type, const std::string &name, implement_create_cb worker);

        /**
         * Get a transport protocol worker by its type
         * @param type transport protocol type
         * @return the list of implements
         */
        const std::map <std::string, implement_create_cb> &transport_protocol_worker (const std::string &type);

        /**
         * Add a protocol worker
         *
         * @param type protocol type
         * @param name protocol name
         * @param worker worker callback
         */

        void http_protocol_worker (http::versions::http type, const std::string &name, implemenet_http_cb worker);

        /**
         * get implementations of the protocol worker by its type
         * @param type protocol type
         * @return the implementations
         */
        const std::map <std::string, implemenet_http_cb> &http_protocol_worker (http::versions::http type);

        /**
         * set a config file path
         * @param path file path
         * @return a future
         */
        manapi::future<manapi::error::status> config (std::string path);

        /**
         * set a config JSON object
         * @param config config JSON object
         * @return a future
         */
        manapi::future<manapi::error::status> config_object (json config);

        // const manapi::json &config ();

        /**
         * Get compressed file by its file path
         * @param file file path
         * @param algorithm algorithm (brotli, gzip and etc)
         * @param filetime file time
         * @return InternalError, NotFound, Unavailable on error
         */
        manapi::future<manapi::error::status_or<std::string>> get_compressed_cache_file (std::string file, std::string algorithm, std::chrono::system_clock::time_point filetime);

        /**
         * Set compressed by file path
         * @param file File Path
         * @param compressed Compressed Path
         * @param algorithm Algorithm (brotli, gzip and etc)
         * @param filetime File Time
         * @return OK if there's no error, otherwise - InternalError
         */
        manapi::future<manapi::error::status> set_compressed_cache_file (std::string file, std::string compressed, std::string algorithm, std::chrono::system_clock::time_point filetime);

        /**
         * Set not avaiable file for compressing for other threads
         * @param file File Path
         * @param lock Lock Status
         * @param algorithm Algorithm (brotli, gzip and etc)
         * @return InternalError, Unavailable on error
         */

        manapi::future<manapi::error::status> set_locked_cache_file (std::string file, bool lock, std::string algorithm);

        [[nodiscard]] const std::string &config_cache_dir();

    protected:
        static void on_config_update (std::shared_ptr<data_t> data, const manapi::json &n);

        void setup ();

        manapi::future<> setup_config (manapi::json &n);

        static manapi::future<> save_config (std::shared_ptr<data_t> data);

        std::shared_ptr<data_t> data;

        static std::string_view default_config_name;
    private:
        static http_handler_function default_error_handler;

        static void check_exists_method_on_url (const std::string &url, const std::unique_ptr<handlers_types_t> &m, const std::string &method);

        static void check_exists_method_on_url (const std::string &url, const std::unique_ptr<handlers_static_types_t> &m, const std::string &method);

        http_uri_part *build_uri_part (const std::string &uri, size_t &type);

    };
}