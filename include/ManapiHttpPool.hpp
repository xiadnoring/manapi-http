#ifndef MANAPIHTTPPOOL_H
#define MANAPIHTTPPOOL_H

#include <ev++.h>
#include <netdb.h>
#include <list>
#include <future>
#include <openssl/ssl.h>
#include <quiche.h>

#include "ManapiHttpConfig.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"
#include "ManapiTask.hpp"
#include "ManapiSite.hpp"

namespace manapi::net {
    class http_pool {
    public:
        explicit http_pool(const json &config, class site *site, const size_t &id);
        ~http_pool();

        ev::loop_ref                get_loop ();

        void                        stop ();
        std::future <int>           run ();

        void                        new_connection_quic    (ev::io &watcher, int revents);
        void                        new_connection_tls     (ev::io &watcher, int revents);

        class site                  &get_site () const;

        // quic data
        QUIC_MAP_CONNS_T            quic_map_conns;

        utils::safe_unordered_map <utils::manapi_socket_information, task *> peer_by_ip;
        std::mutex                  recv_m;

        const int                   &get_fd ();
    private:
        int                         _pool ();
        static SSL_CTX*             ssl_create_context (const size_t &version = versions::TLS_v1_3);
        void                        ssl_configure_context ();

        size_t                      id;

        class config                config;

        // pool

        std::mutex                  m_running;
        std::mutex                  m_initing;

        ev::dynamic_loop            loop;
        addrinfo                    *local          = nullptr;
        std::promise <int>          *pool_promise   = nullptr;

        class site                  *site;
        // watchers
        ev::io                      *ev_io = nullptr;
    };
}

#endif //MANAPIHTTPPOOL_H
