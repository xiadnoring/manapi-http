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
    typedef std::map<std::thread::id, std::map<size_t, std::unique_ptr<http_pool>>> pools_t;
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

        server(const async::shared_ctx &ctx);
        ~server() final;

        server(server &&n) noexcept;
        server&operator=(server &&n) noexcept;

        server(const server &n);
        server&operator=(const server &n);

        manapi::future <void> start (std::vector<async::shared_cthread> loops = {});

        void GET (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);
        void POST (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);
        void OPTIONS(std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);
        void PUT (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);
        void PATCH (std::string uri, handler_template_t handler, json_mask get_mask = nullptr, json_mask post_mask = nullptr);

        void GET (std::string uri, std::string folder);

        manapi::future<void> stop ();
    private:
        std::vector<async::shared_cthread> loops_;
        std::shared_ptr<data2_t> data2;
        manapi::future<void> stop_ (bool evloop);
        manapi::future<> init_pool_ ();
        manapi::future<> call_in_thread_ (const async::shared_cthread &thr, std::move_only_function<manapi::future<>()> cb);
        manapi::future<void> pool_ (std::move_only_function<void()> cb);
        void clean_up ();
        manapi::future<void> stop_pool ();
    };
}
