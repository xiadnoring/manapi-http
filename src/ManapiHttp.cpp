#include <iostream>
#include <csignal>
#include <memory>
#include <utility>
#include <vector>
#include <memory.h>
#include <arpa/inet.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <netdb.h>

#include "ManapiTaskFunction.hpp"
#include "ManapiHttp.hpp"
#include "ManapiTaskHttp.hpp"
#include "ManapiUtils.hpp"

bool        manapi::net::http::stopped_interrupt        = false;

std::vector <manapi::net::http *> manapi::net::http::running;

void handler_interrupt (int sig)
{
    manapi::net::http::stopped_interrupt = true;

    for (auto it = manapi::net::http::running.begin(); it != manapi::net::http::running.end(); )
    {
        (*it)->stop();
        it = manapi::net::http::running.erase(it);
    }

    if (sig == SIGFPE)
    {
        // TODO: MORE INFORMATION!!!
        exit (sig);
    }
}

manapi::net::http::~http() = default;

manapi::net::http::http() {
    signal  (SIGPIPE, SIG_IGN);

    //signal  (SIGFPE, handler_interrupt);
    signal  (SIGABRT, handler_interrupt);
    signal  (SIGKILL, handler_interrupt);
    signal  (SIGTERM, handler_interrupt);


    stopping = false;

    setup ();
}

std::future<void> manapi::net::http::pool(const size_t &thread_num) {
    {
        m_running.lock();
        std::lock_guard <std::mutex> lk (m_initing);

        tasks_pool_init(thread_num);
        setup_timer_pool (get_tasks_pool().get());

        pool_promise = std::make_unique<std::promise<void>>();

        // init all pools
        if (config.contains("pools"))
        {
            for (auto it = config["pools"].begin<utils::json::ARRAY>(); it != config["pools"].end<utils::json::ARRAY>(); it++, next_pool_id++)
            {
                auto p = new http_pool (*it, this, next_pool_id);
                pools.insert({next_pool_id, p});
                p->run();
            }
        }

        std::thread stop_thread ([this] () -> void {
            utils::before_delete unwrap_stop_pool ([this] () -> void {
                pool_promise->set_value();
                m_running.unlock();
            });

            stop_pool();
        });
        stop_thread.detach();
    }

    return pool_promise->get_future();
}

void manapi::net::http::GET(const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask, const utils::json_mask &post_mask) {
    this->set_handler("GET", uri, handler, get_mask, post_mask);
}

void manapi::net::http::POST(const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask, const utils::json_mask &post_mask) {
    this->set_handler("POST", uri, handler, get_mask, post_mask);
}

void manapi::net::http::OPTIONS(const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask, const utils::json_mask &post_mask) {
    this->set_handler("OPTIONS", uri, handler, get_mask, post_mask);
}

void manapi::net::http::PUT(const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask, const utils::json_mask &post_mask) {
    this->set_handler("PUT", uri, handler, get_mask, post_mask);
}

void manapi::net::http::PATCH(const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask, const utils::json_mask &post_mask) {
    this->set_handler("PATCH", uri, handler, get_mask, post_mask);
}

void manapi::net::http::DELETE(const std::string &uri, const handler_template_t &handler, const utils::json_mask &get_mask, const utils::json_mask &post_mask) {
    this->set_handler("DELETE", uri, handler, get_mask, post_mask);
}

void manapi::net::http::GET(const std::string &uri, const std::string &folder) {
    set_handler ("GET", uri, folder);
}

void manapi::net::http::stop(bool wait) {
    {
        std::lock_guard <std::mutex> lk (m_initing);
        stopping = true;
    }
    {
        cv_stopping.notify_one();

        if (wait)
        {
            std::lock_guard <std::mutex> lk (m_running);
        }
    }
}

void manapi::net::http::stop_pool() {
    std::unique_lock <std::mutex> lk (m_stopping);

    http::running.push_back(this);

    cv_stopping.wait(lk, [this] () -> bool { return stopping; });

    ////////////////////////
    //////// Stopping //////
    ////////////////////////

    tasks_pool_stop();

    // stop all pools
    for (const auto &pool: pools)
    {
        MANAPI_LOG ("pool #{} is stopping...", pool.first);
        pool.second->stop();
        delete pool.second;
        MANAPI_LOG ("pool #{} stopped successfully", pool.first);
    }

    pools.clear();
    next_pool_id = 0;
    stopping = false;

    this->save();

    // do we need to delete it?
    if (!http::stopped_interrupt)
    {
        for (auto it = http::running.begin(); it != http::running.end(); it++)
        {
            if (*it == this)
            {
                http::running.erase(it);
                break;
            }
        }
    }

    MANAPI_LOG("{}", "all tasks are closed");
}
