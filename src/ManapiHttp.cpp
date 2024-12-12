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

#include "services/ManapiTaskFunction.hpp"
#include "ManapiHttp.hpp"
#include "ManapiUtils.hpp"

bool manapi::net::http::server::stopped_interrupt = false;

std::vector <manapi::net::http::server *> manapi::net::http::server::running;

void handler_interrupt (int sig)
{
    manapi::net::http::server::stop_all_servers();

    if (sig == SIGFPE)
    {
        // TODO: MORE INFORMATION!!!
        exit (sig);
    }
}

manapi::net::http::server::~server() = default;

manapi::net::http::server::server() {
    signal  (SIGPIPE, SIG_IGN);

    //signal  (SIGFPE, handler_interrupt);
    signal  (SIGABRT, handler_interrupt);
    signal  (SIGKILL, handler_interrupt);
    signal  (SIGTERM, handler_interrupt);
    signal (SIGSTOP, handler_interrupt);


    stopping.store(false);

    setup ();
}

std::future<void> manapi::net::http::server::pool(const size_t &thread_num) {
    {
        m_running.lock();
        std::lock_guard <std::mutex> lk (m_initing);

        tasks_pool_init(thread_num);
        timer_pool_setup (get_tasks_pool().get());

        pool_promise = std::make_unique<std::promise<void>>();

        // init all pools
        if (config.contains("pools"))
        {
            for (auto it = config["pools"].begin<json::ARRAY>(); it != config["pools"].end<json::ARRAY>(); it++, next_pool_id++)
            {
                auto p = std::make_unique<http_pool> (*it, this, next_pool_id);
                p->run();
                pools.insert({next_pool_id, std::move(p)});
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

void manapi::net::http::server::GET(const std::string &uri, const handler_template_t &handler, const json_mask &get_mask, const json_mask &post_mask) {
    this->set_handler("GET", uri, handler, get_mask, post_mask);
}

void manapi::net::http::server::POST(const std::string &uri, const handler_template_t &handler, const json_mask &get_mask, const json_mask &post_mask) {
    this->set_handler("POST", uri, handler, get_mask, post_mask);
}

void manapi::net::http::server::OPTIONS(const std::string &uri, const handler_template_t &handler, const json_mask &get_mask, const json_mask &post_mask) {
    this->set_handler("OPTIONS", uri, handler, get_mask, post_mask);
}

void manapi::net::http::server::PUT(const std::string &uri, const handler_template_t &handler, const json_mask &get_mask, const json_mask &post_mask) {
    this->set_handler("PUT", uri, handler, get_mask, post_mask);
}

void manapi::net::http::server::PATCH(const std::string &uri, const handler_template_t &handler, const json_mask &get_mask, const json_mask &post_mask) {
    this->set_handler("PATCH", uri, handler, get_mask, post_mask);
}

void manapi::net::http::server::DELETE(const std::string &uri, const handler_template_t &handler, const json_mask &get_mask, const json_mask &post_mask) {
    this->set_handler("DELETE", uri, handler, get_mask, post_mask);
}

void manapi::net::http::server::GET(const std::string &uri, const std::string &folder) {
    set_handler ("GET", uri, folder);
}

void manapi::net::http::server::stop(bool wait) {
    {
        std::lock_guard <std::mutex> lk (m_initing);
        stopping.store(true);
    }
    {
        cv_stopping.notify_all();

        if (wait)
        {
            std::lock_guard <std::mutex> lk (m_running);
        }
    }
}

manapi::net::async_delay manapi::net::http::server::delay(const std::chrono::seconds &n) {
    return {*timerpool, n};
}

void manapi::net::http::server::stop_all_servers() {
    manapi::net::http::server::stopped_interrupt = true;

    for (auto it = manapi::net::http::server::running.begin(); it != manapi::net::http::server::running.end(); )
    {
        (*it)->stop();
        it = manapi::net::http::server::running.erase(it);
    }
}

void manapi::net::http::server::stop_pool() {
    std::unique_lock <std::mutex> lk (m_stopping);

    server::running.push_back(this);

    cv_stopping.wait(lk, [this] () -> bool { return stopping.load(); });

    MANAPIHTTP_LOG2("cv_stopping -> pass");
    timer_pool_stop();
    MANAPIHTTP_LOG2("timer_pool_stop(...) -> pass");

    // stop all pools
    for (const auto &pool: pools)
    {
        MANAPIHTTP_LOG ("pool #{} is stopping...", pool.first);
        pool.second->stop();
        MANAPIHTTP_LOG ("pool #{} stopped successfully", pool.first);
    }
    pools.clear();
    MANAPIHTTP_LOG2("pools(...) -> pass");

    tasks_pool_stop();

    MANAPIHTTP_LOG2("tasks_pool_stop(...) -> pass");

    // reset
    next_pool_id = 0;
    stopping.store(false);

    this->save();

    // do we need to delete it?
    if (!server::stopped_interrupt)
    {
        for (auto it = server::running.begin(); it != server::running.end(); it++)
        {
            if (*it == this)
            {
                server::running.erase(it);
                break;
            }
        }
    }

    MANAPIHTTP_LOG2("all tasks are closed");
}
