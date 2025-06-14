#pragma once

#include <thread>
#include "../ManapiUtils.hpp"
#include "../ManapiSite.hpp"
#include "./base_worker.hpp"
#include "../components/ManapiBuffer.hpp"

namespace manapi::net::http {
    struct http_v2_t;
    struct http_v2_stream_t;
}

namespace manapi::net::worker {
    class http_v2 final : public worker::base {
    public:
        http_v2 (worker::base *w);

        ~http_v2 ();

        const std::shared_ptr<worker_config_t> &worker_data() override;

        http::config *config() override;

        http::site &site() override;

        void waiting(const shared_conn &conn, bool state) override;

        void feed_event(const shared_conn &conn, int flags, const char *buff, ssize_t size, ibuffpool_t *p) override;

        void close_connection(shared_conn conn, bool clean_disconnect) override;

        void configure_connection(const shared_conn &conn, oncont_cb cb) override;

        future<ssize_t> response(const shared_conn &connection, http::response *resp, bool finish) override;

        int event_flags(const shared_conn & conn) override;

        int event_flags(const shared_conn & conn, int flags) override;

        std::unique_ptr<worker_watcher_cb> event_on(const shared_conn & conn, std::unique_ptr<worker_watcher_cb> callback) override;

        void init() override;

        connection::ipdata_t *ipdata(worker::connection *conn) override;

        bool is_writable(const shared_conn &conn) override;

        bool is_valid_connection(worker::connection *connection) override;

        void stop(std::function<void()> cb) override;

        ssize_t sync_write(const shared_conn &conn, const void *buff, ssize_t size, bool finish) override;

        ssize_t sync_write_ex(const shared_conn &conn, const void *buff, ssize_t size, bool finish, int maxcnt) override;

        void update_limit_rate_stream (const shared_conn &conn);
    private:
        worker::base *w;
    };
}
