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
#include "ManapiSite.hpp"
#include "services/ManapiThreadPool.hpp"
#include "ManapiJsonMask.hpp"
#include "ManapiHttpPool.hpp"
#include "services/ManapiTimerPool.hpp"

#include "ManapiHttpResponse.hpp"
#include "ManapiHttpRequest.hpp"
#include "async/ManapiAsyncPromise.hpp"
#include "services/ManapiEventLoop.hpp"

namespace manapi::net::http {
    using pools_t = std::map<std::thread::id, std::map<size_t, std::unique_ptr<http_pool>>>;
    class server : public site {
        struct data2_t {
            std::unique_ptr<async::mutex> mx;
            std::atomic <bool> stopping;
            pools_t pools;
            std::size_t event_id;
            std::size_t clean_up_id;
            std::size_t next_pool_id;
            async::promise<void>::resolve_t resolve_stop;
            std::shared_ptr<ev::async> init_watcher;
        };
    public:
        using resp = manapi::net::http::response &;
        using req = manapi::net::http::request &;

        server(server_ctx sctx);
        ~server() final;

        server(server &&n) noexcept;
        server&operator=(server &&n) noexcept;

        server(const server &n);
        server&operator=(const server &n);

        manapi::future <void> start ();

        void GET (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);
        void POST (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);
        void OPTIONS(std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);
        void PUT (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);
        void PATCH (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        void GET (std::string uri, std::string folder, handler_template_t handler = nullptr, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        manapi::future<void> stop ();
    private:
        std::shared_ptr<data2_t> data2;
        static manapi::future<void> stop_ (std::shared_ptr<site::data_t> data, std::shared_ptr<data2_t> data2, bool evloop);
        manapi::future<> init_pool_ ();
        manapi::future<> call_in_thread_ (const async::shared_cthread &thr, std::move_only_function<manapi::future<>()> cb);
        void pool_ (std::move_only_function<void()> cb);
        static void clean_up (std::shared_ptr<data2_t> data2);
        static manapi::future<void> stop_pool (std::shared_ptr<data2_t> data2);
    };
}
