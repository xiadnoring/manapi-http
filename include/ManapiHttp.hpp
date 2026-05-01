#pragma once

#include <functional>
#include <map>
#include <thread>
#include <future>

#include "./ManapiUtils.hpp"
#include "./worker/ManapiSite.hpp"
#include "./ManapiThreadPool.hpp"
#include "./json/ManapiJsonMask.hpp"
#include "./http/ManapiHttpPool.hpp"

#include "./http/ManapiHttpResponse.hpp"
#include "./http/ManapiHttpRequest.hpp"

namespace manapi::net::http {
    using pools_t = std::map<std::thread::id, std::map<size_t, std::unique_ptr<http_pool>>>;

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

        manapi::status_or<http::async_handler_t*> async_cb () MANAPIHTTP_NOEXCEPT;

        manapi::status_or<http::sync_handler_t*> sync_cb () MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD operator bool () const MANAPIHTTP_NOEXCEPT;
    private:
        int m_type;

        void *m_data;
    };

    class server : public net::worker::site, public std::enable_shared_from_this<server> {
        /**
         * initialize the server with the server ctx
         * @param sctx the HTTP server context
         */
        server(std::shared_ptr<server_ctx> sctx);
    public:
        struct data_t;

        using resp = manapi::net::http::response &;
        using req = manapi::net::http::request &;
        using uresp = manapi::net::http::uresponse;

        // Compress file callback
        typedef std::move_only_function<future<manapi::status>(std::string src, std::string dest)> compress_file_cb_t;

        // Compress string callback
        typedef std::move_only_function<manapi::status_or<std::string>(std::string_view data)> compress_str_cb_t;

        // Worker init callbacks
        typedef std::function<std::shared_ptr<worker::base>(std::shared_ptr<net::worker::site> site, std::shared_ptr<multithread_storage::worker_t> wdata, http::config* config)> implement_create_cb;

        // Http init callback
        typedef std::function<manapi::status_or<std::unique_ptr<worker::wrk_interface_global_t>> (worker::interface_worker *w)> implemenet_http_cb;

        /**
         * initialize the server with the server ctx
         * @param sctx the HTTP server context
         */
        static manapi::status_or<std::shared_ptr<server>> create (std::shared_ptr<server_ctx> sctx) MANAPIHTTP_NOEXCEPT;

        /**
         * Casts |worker::site| to |http::server|
         * @param serv
         * @return
         */
        static server *cast (worker::site *serv);

        /**
         * deconstructor
         */
        ~server() override;

        /**
         * Set a handler by its method and URI
         * @param method method
         * @param uri URI path
         * @param handler handler
         * @return
         */
        manapi::status_or<http_uri_part *> handler (std::string method, std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT;

        /**
         * Set a folder sharing handler by its method and URI
         * @param method method
         * @param uri URI
         * @param folder folder
         * @param handler callback
         * @return
         */
        manapi::status_or<http_uri_part *> handler (std::string method, std::string uri, std::string folder, handler_template_t handler = nullptr) MANAPIHTTP_NOEXCEPT;

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

        /**
         * Get file compressor callack by name
         * @param name Name of compressor callback
         * @return compressor callback
         */
        compress_file_cb_t *compressor_for_file (std::string_view name);

        /**
         * Get string compressor callack by name
         * @param name Name of compressor callback
         * @return compressor callback
         */
        compress_str_cb_t *compressor_for_string (std::string_view name);

        /**
         * Find file compressor callback by name
         * @param name Compressor callback name
         * @return true if it exists, otherwise, returns false
         */
        MANAPIHTTP_NODISCARD bool contains_compressor_for_file (std::string_view name) const;

        /**
         * Find string compressor callback by name
         * @param name Compressor callback name
         * @return true if it exists, otherwise, returns false
         */
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
         * start working the server
         * @return InternalError, ResourceExhausted, AlreadyExists on error
         */
        manapi::future <manapi::status> start ();

        /**
         * set the GET method for a path
         * @param uri URI path
         * @param handler callback
         */
        manapi::status GET (std::string uri, handler_template_t handler, manapi::json params = nullptr) MANAPIHTTP_NOEXCEPT;

        /**
         * set the POST method for a path
         * @param uri URI path
         * @param handler callback
         */
        manapi::status POST (std::string uri, handler_template_t handler, manapi::json params = nullptr) MANAPIHTTP_NOEXCEPT;

        /**
         * set the OPTIONS method for a path
         * @param uri URI path
         * @param handler callback
         */
        manapi::status OPTIONS(std::string uri, handler_template_t handler, manapi::json params = nullptr) MANAPIHTTP_NOEXCEPT;

        /**
         * set the PUT method for a path
         * @param uri URI path
         * @param handler callback
         */
        manapi::status PUT (std::string uri, handler_template_t handler, manapi::json params = nullptr) MANAPIHTTP_NOEXCEPT;

        /**
         * set the PATCH method for a path
         * @param uri URI path
         * @param handler callback
         */
        manapi::status PATCH (std::string uri, handler_template_t handler, manapi::json params = nullptr) MANAPIHTTP_NOEXCEPT;

        /**
         * share the folder for the GET method
         * @param uri URI path
         * @param folder folder
         * @param handler callback
         */
        manapi::status GET (std::string uri, std::string folder, handler_template_t handler = nullptr) MANAPIHTTP_NOEXCEPT;

        /**
            * Get compressed file by its file path
            * @param file file path
            * @param algorithm algorithm (brotli, gzip and etc)
            * @param filetime file time
            * @return InternalError, NotFound, Unavailable on error
            */
        manapi::future<manapi::status_or<std::string>> get_compressed_cache_file (std::string file, std::string algorithm, std::chrono::system_clock::time_point filetime);

        /**
         * Set compressed by file path
         * @param file File Path
         * @param compressed Compressed Path
         * @param algorithm Algorithm (brotli, gzip and etc)
         * @param filetime File Time
         * @return OK if there's no error, otherwise - InternalError
         */
        manapi::future<manapi::status> set_compressed_cache_file (std::string file, std::string compressed, std::string algorithm, std::chrono::system_clock::time_point filetime);

        /**
         * Set not avaiable file for compressing for other threads
         * @param file File Path
         * @param lock Lock Status
         * @param algorithm Algorithm (brotli, gzip and etc)
         * @return InternalError, Unavailable on error
         */
        manapi::future<manapi::status> set_locked_cache_file (std::string file, bool lock, std::string algorithm);


        /**
         * Stops the server
         * @return result
         */
        manapi::future<manapi::status> stop () override;

        /**
         * Calls server config setup with provided path to Json file
         * @param path Path to Json file
         * @return
         */
        manapi::future<manapi::status> config(std::string path) override;

        /**
         * Calls server config setup with provided Json object
         * @param config Json object
         * @return result
         */
        manapi::future<manapi::status> config_object(json config) override;

        /**
         * Get config cache directory path
         * @return Path in filesytem
         */
        const std::string & config_cache_dir() const;
    private:
        std::unique_ptr <data_t> m_data;
    };
}
