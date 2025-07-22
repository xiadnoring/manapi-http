#pragma once

#if defined(__unix__) || defined(__APPLE__)
#   include <netinet/in.h>
#endif
#include <functional>
#include <map>
#include <regex>
#include <thread>
#include <future>

#include "ManapiUtils.hpp"
#include "http/ManapiSite.hpp"
#include "services/ManapiThreadPool.hpp"
#include "ManapiJsonMask.hpp"
#include "http/ManapiHttpPool.hpp"
#include "services/ManapiTimerPool.hpp"

#include "ManapiHttpResponse.hpp"
#include "ManapiHttpRequest.hpp"
#include "async/ManapiAsyncPromise.hpp"
#include "services/ManapiEventLoop.hpp"

namespace manapi::net::http {
    using pools_t = std::map<std::thread::id, std::map<size_t, std::unique_ptr<http_pool>>>;
    class server : public site {
        struct data2_t;
    public:
        using resp = manapi::net::http::response &;
        using req = manapi::net::http::request &;

        /**
         * initialize the server with the server ctx
         * @param sctx the HTTP server context
         */
        server(server_ctx sctx);

        /**
         * deconstructor
         */
        ~server() final;

        /* move */
        server(server &&n) noexcept;

        /* move */
        server&operator=(server &&n) noexcept;

        /* copy */
        server(const server &n);

        /* copy */
        server&operator=(const server &n);

        /**
         * start working the server
         * @return InternalError, ResourceExhausted, AlreadyExists on error
         */
        manapi::future <manapi::error::status> start ();

        /**
         * set the GET method for a path
         * @param uri URI path
         * @param handler callback
         * @param get_mask GET mask
         * @param post_mask POST mask
         */
        void GET (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        /**
         * set the POST method for a path
         * @param uri URI path
         * @param handler callback
         * @param get_mask GET mask
         * @param post_mask POST mask
         */
        void POST (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        /**
         * set the OPTIONS method for a path
         * @param uri URI path
         * @param handler callback
         * @param get_mask GET mask
         * @param post_mask POST mask
         */
        void OPTIONS(std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        /**
         * set the PUT method for a path
         * @param uri URI path
         * @param handler callback
         * @param get_mask GET mask
         * @param post_mask POST mask
         */
        void PUT (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        /**
         * set the PATCH method for a path
         * @param uri URI path
         * @param handler callback
         * @param get_mask GET mask
         * @param post_mask POST mask
         */
        void PATCH (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        /**
         * share the folder for the GET method
         * @param uri URI path
         * @param folder folder
         * @param handler callback
         * @param get_mask GET mask
         * @param post_mask  POST mask
         */
        void GET (std::string uri, std::string folder, handler_template_t handler = nullptr, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        /**
         * stop working the server
         * @return the future
         */
        manapi::future<error::status> stop ();
    private:
        std::shared_ptr<data2_t> data2;

        static manapi::future<error::status> stop_ (std::shared_ptr<site::data_t> data, std::shared_ptr<data2_t> data2, bool evloop);

        manapi::future<> init_pool_ ();

        void pool_ (std::move_only_function<void()> cb);

        static void clean_up (std::shared_ptr<data2_t> data2);

        static manapi::future<void> stop_pool (std::shared_ptr<data2_t> data2);
    };
}
