#pragma once

#include <thread>
#include "../ManapiUtils.hpp"
#include "../ManapiSite.hpp"
#include "./base_worker.hpp"
#include "components/Buffer.hpp"

namespace manapi::net::http {
    struct http_v2_t;
}

namespace manapi::net::worker {
    class http_v2 : public worker::base {
    public:
        http_v2 (net::http::site site, bufferpool_t bufferpool, std::shared_ptr<worker::worker_config_t> worker_data);

        ~http_v2 ();

        void close_connection(const shared_conn &conn, bool clean_disconnect) override;

        void configure_connection(const shared_conn &conn, oncont_cb cb) override;

        future<ssize_t> response(const shared_conn &connection, http::response *resp, bool finish) override;

        int event_flags(const shared_conn & conn) override;

        int event_flags(const shared_conn & conn, int flags) override;

        std::unique_ptr<worker_watcher_cb> event_on(const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) override;

        void init() override;

        bool is_writable(const shared_conn &conn) override;

        bool is_valid_connection(worker::connection *connection) override;

        void stop() override;

        ssize_t sync_write(const shared_conn &conn, const void *buff, ssize_t size, bool finish) override;

        ssize_t sync_write_ex(const shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt) override;
    };
}
