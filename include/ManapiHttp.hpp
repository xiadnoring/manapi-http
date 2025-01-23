#pragma once

#if defined(__unix__) || defined(__APPLE__)
#   include <netinet/in.h>
#endif
#include <functional>
#include <map>
#include <regex>
#include <thread>
#include <future>
#include "extensions/ev++.h"

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
    class server : public site {
    public:
        server(const std::shared_ptr<async::context> &ctx);
        ~server() final;
        manapi::future <void> start ();

        void GET (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void POST (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void OPTIONS(const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void PUT (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        // void DELETE (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void PATCH (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);

        void GET (const std::string &uri, const std::string &folder);

        manapi::future<void> stop ();
    private:
        manapi::future<> _init_pool ();
        manapi::future<void> _pool (const std::function<void()> &cb);
        manapi::future<void> stop_pool ();

        async::mutex mx;
        std::atomic <bool> stopping;

        std::unordered_map<size_t, std::unique_ptr<http_pool>> pools{};

        size_t event_id{0};
        size_t next_pool_id = 0;
        async::promise<void>::resolve_t resolve_stop{nullptr};
        std::shared_ptr<ev::async> init_watcher{nullptr};
    };
}
