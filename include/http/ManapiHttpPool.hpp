#pragma once

#include <list>
#include <future>
#include <functional>

#include "./ManapiHttpCtx.hpp"
#include "./ManapiBaseHttp.hpp"
#include "./ManapiHttpConfig.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiUtils.hpp"
#include "../json/ManapiJson.hpp"
#include "../worker/ManapiBaseWorker.hpp"

namespace manapi::net {
    namespace worker {
        class site;
    }

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
        explicit http_pool(const json &config, std::shared_ptr<multithread_storage::worker_t> worker_config, std::shared_ptr<worker::base_http> site, size_t id);

        /* deconstructor */
        ~http_pool();

        /**
         * stop the server
         * @return InternalError, ResourceExhausted on error
         */
        manapi::future<manapi::status> stop ();

        /**
         * start the server
         * @return InternalError, ResourceExausted, FailedPrecondition on error
         */
        manapi::future<manapi::status> run ();

        /**
         * get the site instance
         * @return the site instance
         */
        MANAPIHTTP_NODISCARD const std::shared_ptr<worker::base_http> &site () const;

        MANAPIHTTP_NODISCARD std::shared_ptr <http::config> config () const;
    private:
        manapi::future<manapi::status> pool ();

        std::size_t m_id;

        std::shared_ptr<multithread_storage::worker_t> m_worker_config;

        std::shared_ptr <http::config> m_config;

        std::shared_ptr <worker::base> m_worker;

        // pool

        std::shared_ptr<manapi::async::mutex> m_mx;

        std::shared_ptr<worker::base_http> m_site;
    };
}