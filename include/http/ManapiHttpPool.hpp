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
#include "../std/ManapiStopToken.hpp"

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
        http_pool(const json &config, size_t id);

        ~http_pool();

        /**
         * stop the server
         */
        void send_stop (manapi::stoken token);

        /**
         * start the server
         * @return InternalError, ResourceExausted, FailedPrecondition on error
         */
        manapi::future<manapi::status> run (std::shared_ptr<worker::base_http> site, manapi::net::worker::worker_data_t *wdata);

        MANAPIHTTP_NODISCARD const std::shared_ptr <manapi::net::http::config> &config() const;
    private:
        std::size_t m_id;

        std::shared_ptr <worker::base> m_worker;

        std::shared_ptr <http::config> m_config;
    };
}