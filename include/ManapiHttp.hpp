#pragma once

#include <functional>
#include <map>
#include <thread>
#include <future>

#include "./ManapiUtils.hpp"
#include "./http/ManapiSite.hpp"
#include "./ManapiThreadPool.hpp"
#include "./json/ManapiJsonMask.hpp"
#include "./http/ManapiHttpPool.hpp"

#include "./http/ManapiHttpResponse.hpp"
#include "./http/ManapiHttpRequest.hpp"

namespace manapi::net::http {
    using pools_t = std::map<std::thread::id, std::map<size_t, std::unique_ptr<http_pool>>>;
    class server : public site {
        struct data2_t;

    public:
        using resp = manapi::net::http::response &;
        using req = manapi::net::http::request &;
        using uresp = manapi::net::http::uresponse;

        /**
         * creates empty server
         */
        server();

        /**
         * initialize the server with the server ctx
         * @param sctx the HTTP server context
         */
        server(server_ctx sctx);

        /**
         * initialize the server with the server ctx
         * @param sctx the HTTP server context
         */
        static manapi::status_or<server> create (server_ctx sctx) MANAPIHTTP_NOEXCEPT;

        /**
         * deconstructor
         */
        ~server() final;

        /* move */
        server(server &&n) MANAPIHTTP_NOEXCEPT;

        /* move */
        server&operator=(server &&n) MANAPIHTTP_NOEXCEPT;

        /* copy */
        server(const server &n);

        /* copy */
        server&operator=(const server &n);

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
         * stop working the server
         * @return the future
         */
        manapi::future<manapi::status> stop ();
    private:
        static manapi::future<manapi::status> stop_ (std::shared_ptr<data2_t> data2, bool evloop);

        manapi::future<> init_pool_ ();

        manapi::status pool_ (std::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT;

        static void clean_up (const std::shared_ptr<data2_t>& data2);

        static manapi::future<void> stop_pool (std::shared_ptr<data2_t> data2);
    };
}
