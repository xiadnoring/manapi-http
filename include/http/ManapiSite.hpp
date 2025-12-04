/**
 * @file http/ManapiSite.hpp
 * @brief Provides the site structs
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <regex>
#include <chrono>

#include "./ManapiHttpUtils.hpp"
#include "./ManapiHttpConfig.hpp"
#include "./ManapiSiteCtx.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../json/ManapiJson.hpp"
#include "../json/ManapiJsonMask.hpp"

namespace manapi::net::worker {
    class base;
    class interface_worker;
    struct wrk_interface_global_t;
}

namespace manapi::net::http {
    class request;
    class response;
    class uresponse;
}

namespace manapi::net::http {
    typedef std::move_only_function <future<>(manapi::net::http::request &req, manapi::net::http::response &res)> async_handler_t;

    typedef std::move_only_function <void (manapi::net::http::request &req, manapi::net::http::uresponse res)> sync_handler_t;

    struct http_handler_function;

    struct http_uri_part;

    struct http_static_handler_function;

    struct http_handler_page;

    struct http_handler_page_error;

    typedef std::map<std::string, std::unique_ptr<http_uri_part>> handlers_map_t;

    typedef std::pair<std::regex, std::unique_ptr<http_uri_part>> handlers_regex_pair_t;

    typedef std::map<std::string, handlers_regex_pair_t> handlers_regex_map_t;

    typedef std::vector<std::string> handlers_regex_titles_t;

    typedef std::map <std::string, http_static_handler_function> handlers_static_types_t;

    typedef std::map <std::string, std::shared_ptr<http_handler_function>> handlers_types_t;

    class handler_template_t {
    public:
        handler_template_t ();

        handler_template_t (const std::nullptr_t &n);

        handler_template_t (handler_template_t &&n) MANAPIHTTP_NOEXCEPT;

        handler_template_t&operator=(handler_template_t &&n) MANAPIHTTP_NOEXCEPT;

        template<typename func>
        requires(!std::is_function_v<func> && std::is_convertible_v<func, async_handler_t>)
        handler_template_t (func a) : handler_template_t (async_handler_t(std::move(a))) {}

        template<typename func>
        requires(!std::is_function_v<func> && std::is_convertible_v<func, sync_handler_t>)
        handler_template_t (func a) : handler_template_t (sync_handler_t(std::move(a))) {}

        explicit handler_template_t (http::async_handler_t cb);

        explicit handler_template_t (http::sync_handler_t cb);

        ~handler_template_t();

        MANAPIHTTP_NODISCARD bool is_async_cb () const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD bool is_sync_cb () const MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<http::async_handler_t*> async_cb () MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<http::sync_handler_t*> sync_cb () MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD operator bool () const MANAPIHTTP_NOEXCEPT;
    private:
        int type;
        void *data;
    };

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

        typedef std::function<std::shared_ptr<worker::base>(site site, std::shared_ptr<multithread_storage::worker_t> wdata, http::config* config)> implement_create_cb;

        typedef std::function<manapi::error::status_or<std::unique_ptr<worker::wrk_interface_global_t>> (worker::interface_worker *w)> implemenet_http_cb;

        struct data_t;

        site ();
        /**
         * initialize the site instance
         * @param sctx
         */
        site (server_ctx sctx);

        /**
         * deconstructor
         */
        virtual ~site();

        site (site &&n) MANAPIHTTP_NOEXCEPT;

        /* move */
        site &operator=(site &&n) MANAPIHTTP_NOEXCEPT;

        /* copy */
        site (const site &n);

        /* move */
        site &operator=(const site &n);

        /**
         * Set a handler by its method and URI
         * @param method method
         * @param uri URI path
         * @param handler handler
         * @return
         */
        manapi::error::status_or<http_uri_part *> handler (std::string method, std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT;

        /**
         * Set a folder sharing handler by its method and URI
         * @param method method
         * @param uri URI
         * @param folder folder
         * @param handler callback
         * @return
         */
        manapi::error::status_or<http_uri_part *> handler (std::string method, std::string uri, std::string folder, handler_template_t handler = nullptr) MANAPIHTTP_NOEXCEPT;

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

        compress_file_cb_t *compressor_for_file (std::string_view name);

        compress_str_cb_t *compressor_for_string (std::string_view name);

        MANAPIHTTP_NODISCARD bool contains_compressor_for_file (std::string_view name) const;

        MANAPIHTTP_NODISCARD bool contains_compressor_for_string (std::string_view name) const;

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

        MANAPIHTTP_NODISCARD const std::string &config_cache_dir();

    protected:
        void init_data_ (server_ctx sctx);

        static void on_config_update (std::shared_ptr<data_t> data, const manapi::json &n) MANAPIHTTP_NOEXCEPT;

        void setup ();

        manapi::future<> setup_config (manapi::json &n);

        static manapi::future<> save_config (std::shared_ptr<data_t> data);

        std::shared_ptr<data_t> data;

        static std::string_view default_config_name;
    private:
        static std::shared_ptr<http_handler_function> default_error_handler;

        http_uri_part *build_uri_part (std::string_view uri, size_t &type);

    };
}