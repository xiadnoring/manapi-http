#pragma once

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

#include <list>
#include <future>
#include <functional>

#include "ManapiUtils.hpp"
#include "ManapiHttpConfig.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"
#include "ManapiSite.hpp"
#include "http/ManapiSiteCtx.hpp"

#include "services/ManapiTask.hpp"
#include "worker/base_worker.hpp"
#include "http/base_http.hpp"

namespace manapi::net {
    class http_pool {
    public:
        explicit http_pool(const json &config, std::shared_ptr<worker::worker_config_t> worker_config, class http::site site, const size_t &id, std::shared_ptr<event_loop> events);
        ~http_pool();

        manapi::future<> stop ();
        manapi::future<void> run ();

        const http::site &get_site () const;
    private:
        manapi::future<void> _pool ();

        size_t id;
        std::shared_ptr<worker::worker_config_t> worker_config;

        std::shared_ptr <http::config> config;
        std::shared_ptr <worker::base> worker;

        // pool

        std::shared_ptr<manapi::async::mutex> mx;

        std::shared_ptr<event_loop> events;
        std::unique_ptr<std::promise <int> > pool_promise;

        http::site site;
        // watchers
        std::shared_ptr <ev::io> watcher;
    };
}