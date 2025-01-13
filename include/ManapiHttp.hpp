#pragma once

#include <netinet/in.h>
#include <functional>
#include <map>
#include <regex>
#include <thread>
#include <future>
#include <ev++.h>

#include "ManapiSite.hpp"
#include "services/ManapiThreadPool.hpp"
#include "ManapiJsonMask.hpp"
#include "ManapiHttpPool.hpp"
#include "services/ManapiTimerPool.hpp"

#include "ManapiHttpResponse.hpp"
#include "ManapiHttpRequest.hpp"
#include "async/ManapiAsyncPromise.hpp"

namespace manapi::net::http {
    class server : public site {
    public:
        server(std::shared_ptr<threadpool<task>> taskpool, std::shared_ptr<manapi::timerpool> timerpool);
        ~server() final;
        manapi::future <void> pool ();
        void pool_sync();

        void GET (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void POST (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void OPTIONS(const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void PUT (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void DELETE (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void PATCH (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);

        void GET (const std::string &uri, const std::string &folder);

        manapi::future<void> stop ();

        static void stop_all_servers ();
        static void signal_init ();
    protected:
        void custom_watcher_fd_async(ev::async &w, int revents) override;
    private:
        void _init_pool ();
        void _pool (const std::function<void()> &cb);
        static std::atomic<bool> stopped_interrupt;
        static Atomic<std::set <server *>> running;
        void stop_pool (async::promise<void>::resolve_t resolve);
        void _async_break_loop (ev::async &watcher, int revents);

        async::mutex mx;
        std::atomic <bool> stopping;

        std::unordered_map<size_t, std::unique_ptr<http_pool>> pools{};

        std::thread::id loop_thread_id{0};
        size_t next_pool_id = 0;
        std::shared_ptr<ev::async> stop_watcher{nullptr};
        async::promise<void>::resolve_t resolve_stop{nullptr};
    };
}
