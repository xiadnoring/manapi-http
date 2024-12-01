#ifndef MANAPIHTTPPOOL_H
#define MANAPIHTTPPOOL_H

#include <ev++.h>
#include <netdb.h>
#include <list>
#include <future>
#include <functional>

#include "ManapiHttpConfig.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"
#include "ManapiTask.hpp"
#include "ManapiSite.hpp"
#include "worker/Base.hpp"
#include "http/Base.hpp"

namespace manapi::net {
    class http_pool {
    public:
        explicit http_pool(const json &config, class site *site, const size_t &id);
        ~http_pool();

        ev::loop_ref get_loop ();

        void stop ();
        std::future <int> run ();

        void new_connection (ev::io &watcher, int revents);

        class site &get_site () const;

        // quic data
        quic_map_conns_t quic_map_conns;

        utils::atomic_map <utils::manapi_socket_information, task *> peer_by_ip;
        std::mutex recv_m;

        const int &get_fd ();
    private:
        int _pool ();

        size_t id;

        std::shared_ptr <http::config> config;
        std::shared_ptr <worker::base> worker;

        // pool

        std::mutex m_running;
        std::mutex m_initing;

        ev::dynamic_loop loop;
        std::unique_ptr<std::promise <int> > pool_promise;

        class site *site;
        // watchers
        std::unique_ptr<ev::io> ev_io;
    };
}

#endif //MANAPIHTTPPOOL_H
