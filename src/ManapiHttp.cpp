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
#include "async/ManapiAsyncPromise.hpp"

manapi::net::http::server::~server() = default;

manapi::net::http::server::server(const std::shared_ptr<threadpool<task>> &taskpool, std::shared_ptr<manapi::timerpool> timerpool, std::shared_ptr<manapi::event_loop> event_loop)
        : site(taskpool, std::move(timerpool), std::move(event_loop)), mx(taskpool) {
    this->stopping.store(true);

    setup ();
}

manapi::future<void> manapi::net::http::server::start() {
    auto lk = co_await this->mx.lock_guard();

    if (!server::stopping.exchange(false)) {
        co_return;
    }

    co_await this->_init_pool();

    this->event_id = co_await this->events->subscribe_finish([this] () -> future<> {
        return this->stop();
    });


    co_await async::promise<void> (this->taskpool, [this, &lk] (async::promise<void>::resolve_ref_t resolve, async::promise<void>::reject_ref_t reject) -> void {
        async::run(this->taskpool, [this, &lk, resolve] () mutable -> future<> {
            co_await this->_pool([&lk, resolve = std::move(resolve)] () mutable -> void {
                lk.call();
                resolve ();
            });
        });
    });
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

manapi::future<void> manapi::net::http::server::stop() {
    auto lk = co_await this->mx.lock_guard();

    if (server::stopping.exchange(true)) {
        co_return;
    }

    co_await this->events->unsubscribe_finish(std::exchange(this->event_id, 0));
    co_await this->stop_pool();
}

manapi::future<> manapi::net::http::server::_init_pool() {
    // init all pools
    if (this->config.contains("pools"))
    {
        for (auto it = this->config["pools"].begin<json::ARRAY>(); it != this->config["pools"].end<json::ARRAY>(); ++it, this->next_pool_id++)
        {
            auto p = std::make_unique<http_pool> (*it, this, this->next_pool_id, this->get_event_loop());
            co_await p->run();
            this->pools.insert({this->next_pool_id, std::move(p)});
        }
    }
}

manapi::future<void> manapi::net::http::server::_pool(const std::function<void()> &cb) {
    this->init_watcher = co_await this->events->watch_async([cb] (ev::async &w, int revents) -> void {
        w.stop();
        cb();
    });

    this->init_watcher->send();
}

manapi::future<> manapi::net::http::server::stop_pool() {
    MANAPIHTTP_LOG2("cv_stopping -> pass");

    // stop all pools
    for (const auto &pool: pools)
    {
        MANAPIHTTP_LOG ("pool #{} is stopping...", pool.first);
        pool.second->stop();
        MANAPIHTTP_LOG ("pool #{} stopped successfully", pool.first);
    }
    this->pools.clear();
    MANAPIHTTP_LOG2("pools(...) -> pass");

    this->save();

    // reset
    this->next_pool_id = 0;

    if (this->init_watcher) {
        co_await this->events->unwatch_async(this->init_watcher);
        this->init_watcher.reset();
    }
}