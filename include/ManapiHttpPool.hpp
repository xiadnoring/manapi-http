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
#include "worker/ManapiBaseWorker.hpp"
#include "http/ManapiBaseHttp.hpp"

namespace manapi::net {
    class http_pool {
    public:
        /**
         * initialize the http pool instance
         * @param config config
         * @param worker_config worker config
         * @param site http site
         * @param id ID
         * @param events event loop
         */
        explicit http_pool(const json &config, std::shared_ptr<multithread_storage::worker_t> worker_config, class http::site site, size_t id, std::shared_ptr<event_loop> events);

        /* deconstructor */
        ~http_pool();

        /**
         * stop the server
         * @return InternalError, ResourceExhausted on error
         */
        manapi::future<manapi::error::status> stop ();

        /**
         * start the server
         * @return InternalError, ResourceExausted, FailedPrecondition on error
         */
        manapi::future<manapi::error::status> run ();

        /**
         * get the site instance
         * @return the site instance
         */
        const http::site &get_site () const;
    private:
        manapi::future<manapi::error::status> pool_ ();

        std::size_t id;

        std::shared_ptr<multithread_storage::worker_t> worker_config;

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