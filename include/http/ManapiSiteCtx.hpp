#pragma once

#include <atomic>

#include "../ManapiJson.hpp"
#include "../async/ManapiAsyncThreadsMutex.hpp"

namespace manapi::net {
    namespace worker {
        struct worker_config_t {
            std::unique_ptr<manapi::async::tmutex> mx;
            std::atomic<ssize_t> count;
            manapi::json data;
        };

        struct server_config_t {
            std::unique_ptr<manapi::async::tmutex> cache_mx;
            std::unique_ptr<manapi::async::tmutex> config_mx;
            std::atomic<size_t> config_time;
            std::atomic<size_t> cache_time;
            std::atomic<int> flags;
            manapi::json config;
            manapi::json cache;
        };
    }

    class server_ctx {
        struct data_t {
            std::mutex mx;
            std::vector<std::shared_ptr<worker::worker_config_t>> workers;
            std::shared_ptr<worker::server_config_t> servers;
            std::vector<ev::shared_async> server_subs;
        };
    public:
        server_ctx ();

        ~server_ctx ();

        server_ctx (server_ctx &&n) noexcept;

        server_ctx &operator=(server_ctx &&n) noexcept;

        server_ctx (const server_ctx &n);

        server_ctx &operator=(const server_ctx &n);

        std::shared_ptr<worker::worker_config_t> worker_config (std::size_t id);

        std::shared_ptr<worker::server_config_t> server_config (ev::shared_async w);

        static void next_time (std::atomic<size_t> *n);

        void server_notify_subs ();

        void remove_server_sub (ev::shared_async w);

        void remove_workers ();
    private:
        std::shared_ptr<data_t> data_;
    };
}