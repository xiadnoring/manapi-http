#pragma once

#include "extensions/ev++.h"

#if defined(__unix__)||defined(__APPLE__)
#   include <netdb.h>
#endif

#include <list>
#include <future>
#include <functional>

#include "ManapiHttpConfig.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"
#include "ManapiSite.hpp"

#include "services/ManapiTask.hpp"
#include "worker/base_worker.hpp"
#include "http/base_http.hpp"

namespace manapi::net {
    class http_pool {
    public:
        explicit http_pool(const json &config, class site *site, const size_t &id, std::shared_ptr<event_loop> events);
        ~http_pool();

        void stop ();
        manapi::future<void> run ();

        class site &get_site () const;
    private:
        manapi::future<void> _pool ();

        size_t id;

        std::shared_ptr <http::config> config;
        std::shared_ptr <worker::base> worker;

        // pool

        std::mutex mx;

        std::shared_ptr<event_loop> events;
        std::unique_ptr<std::promise <int> > pool_promise;

        class site *site;
        // watchers
        std::shared_ptr <ev::io> watcher;
    };
}