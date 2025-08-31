#pragma once

#include <list>
#include <future>
#include <functional>

#include "./ManapiSite.hpp"
#include "./ManapiSiteCtx.hpp"
#include "./ManapiBaseHttp.hpp"
#include "./ManapiHttpConfig.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiUtils.hpp"
#include "../json/ManapiJson.hpp"
#include "../worker/ManapiBaseWorker.hpp"

namespace manapi::net {
    class DLLExportImport http_pool {
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
        MANAPIHTTP_NODISCARD http::site site () const;

        MANAPIHTTP_NODISCARD std::shared_ptr <http::config> config () const;
    private:
        manapi::future<manapi::error::status> pool_ ();

        std::size_t id;

        std::shared_ptr<multithread_storage::worker_t> worker_config;

        std::shared_ptr <http::config> config_;

        std::shared_ptr <worker::base> worker;

        // pool

        std::shared_ptr<manapi::async::mutex> mx;

        std::shared_ptr<event_loop> events;

        std::unique_ptr<std::promise <int> > pool_promise;

        http::site site_;
        // watchers
        std::shared_ptr <ev::io> watcher;
    };
}