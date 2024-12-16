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

std::atomic<bool> manapi::net::http::server::stopped_interrupt = false;

manapi::net::Atomic<std::set <manapi::net::http::server *>> manapi::net::http::server::running;

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
    signal (SIGPIPE, SIG_IGN);
    signal (SIGABRT, handler_interrupt);
    signal (SIGKILL, handler_interrupt);
    signal (SIGTERM, handler_interrupt);
    signal (SIGSTOP, handler_interrupt);


    stopping.store(false);

    setup ();
}

std::future<void> manapi::net::http::server::pool(const size_t &thread_num) {
    {
        if (stopping) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "Application was stopped");
        }

        std::unique_lock <std::mutex> lk (mx);

        running.update([this] (auto &v) {
            v.insert(this);
        });

        tasks_pool_init(thread_num);
        timer_pool_setup (get_tasks_pool().get());

        pool_promise = std::make_unique<std::promise<void>>();

        // init all pools
        if (config.contains("pools"))
        {
            for (auto it = config["pools"].begin<json::ARRAY>(); it != config["pools"].end<json::ARRAY>(); it++, next_pool_id++)
            {
                auto p = std::make_unique<http_pool> (*it, this, this->next_pool_id, this->loop);
                p->run();
                pools.insert({next_pool_id, std::move(p)});
            }
        }

        this->taskspool->append_task([this] () -> void {
            loop.run(ev::AUTO);
            pool_promise->set_value();
        });
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

manapi::net::future<void> manapi::net::http::server::stop() {
    this->mx.lock();
    if (this->stopping.load() == true) { co_return; }
    this->stopping.store(true);
    co_await async_thread ([this] () -> void {
        this->stop_pool();
        this->mx.unlock();
    });
}

void manapi::net::http::server::stop_all_servers() {
    manapi::net::http::server::stopped_interrupt.store(true);

    while (!running.get()->empty())
    {
        auto it = *running.get()->begin();
        it->stop().get();
    }
}

void manapi::net::http::server::stop_pool() {
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

    this->loop.break_loop(ev::ALL);
    tasks_pool_stop();

    MANAPIHTTP_LOG2("tasks_pool_stop(...) -> pass");

    // reset
    next_pool_id = 0;
    stopping.store(false);

    this->save();

    running.update([this] (auto &v) -> void {
         v.erase(this);
    });

    MANAPIHTTP_LOG2("all tasks are closed");
}
