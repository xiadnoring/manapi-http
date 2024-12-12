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

namespace manapi::net::http {
    class server : public site {
    public:
        server();
        ~server();
        std::future <void> pool (const size_t &thread_num = 20);

        void GET (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void POST (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void OPTIONS(const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void PUT (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void DELETE (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);
        void PATCH (const std::string &uri, const handler_template_t &handler, const json_mask &get_mask = nullptr, const json_mask &post_mask = nullptr);

        void GET (const std::string &uri, const std::string &folder);

        void stop (bool wait = false);

        manapi::net::async_delay delay (const std::chrono::seconds &n);

        static void stop_all_servers ();
    private:
        static bool stopped_interrupt;
        static std::vector <server *> running;
        void stop_pool ();

        class site current;

        std::mutex m_initing;
        std::mutex m_running;
        std::mutex m_stopping;
        std::condition_variable cv_stopping;
        std::atomic <bool> stopping;

        std::unique_ptr<std::promise <void>> pool_promise;

        std::unordered_map<size_t, std::unique_ptr<http_pool>> pools;

        size_t next_pool_id = 0;
    };
}