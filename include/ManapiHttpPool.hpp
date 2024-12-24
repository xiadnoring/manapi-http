#pragma once

#include <ev++.h>
#include <netdb.h>
#include <list>
#include <future>
#include <functional>

#include "ManapiHttpConfig.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"
#include "ManapiSite.hpp"

#include "services/ManapiTask.hpp"
#include "worker/Base.hpp"
#include "http/Base.hpp"

namespace manapi::net {
    class http_pool {
    public:
        explicit http_pool(const json &config, class site *site, const size_t &id, ev::loop_ref loop);
        ~http_pool();

        ev::loop_ref get_loop ();

        void stop ();
        void run ();

        class site &get_site () const;

        // quic data
        quic_map_conns_t quic_map_conns;

        utils::atomic_map <utils::manapi_socket_information, task *> peer_by_ip;
        std::mutex recv_m;
    private:
        int _pool ();

        size_t id;

        std::shared_ptr <http::config> config;
        std::shared_ptr <worker::base> worker;

        // pool

        std::mutex mx;

        ev::loop_ref loop;
        std::unique_ptr<std::promise <int> > pool_promise;

        class site *site;
        // watchers
        std::shared_ptr <ev::io> watcher;
        std::shared_ptr <ev::async> async_watcher;
    };
}