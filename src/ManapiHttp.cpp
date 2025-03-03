#include <iostream>
#include <csignal>
#include <memory>
#include <utility>
#include <vector>
#include <memory.h>
#if defined(__unix__) || defined(__APPLE__)
#   include <netinet/in.h>
#   include <netdb.h>
#endif
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>

#include "services/ManapiTaskFunction.hpp"
#include "ManapiHttp.hpp"
#include "ManapiUtils.hpp"
#include "async/ManapiAsyncPromise.hpp"

manapi::net::http::server::~server() = default;

manapi::net::http::server::server(const std::shared_ptr<manapi::async::context> &ctx)
        : site(ctx), mx(ctx) {

    this->stopping.store(true);
    setup ();
}

manapi::future<void> manapi::net::http::server::start() {
    // initialization object pools
    this->bufferpool().init(0);

    auto lk = co_await this->mx.lock_guard();

    if (!server::stopping.exchange(false)) {
        co_return;
    }

    co_await this->_init_pool();

    this->event_id = co_await this->ctx->eventloop()->subscribe_finish([this] () -> future<> {
        co_return co_await this->stop();
    });


    co_await async::promise<void> (this->ctx, [this, &lk] (async::promise<void>::resolve_ref_t resolve, async::promise<void>::reject_ref_t reject) -> future<> {
        co_await this->_pool([&lk, resolve] () mutable -> void {
            lk.call();
            resolve ();
        });
    });
}

void manapi::net::http::server::GET(std::string uri, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    this->set_handler("GET", std::move(uri), std::move(handler), std::move(get_mask), std::move(post_mask));
}

void manapi::net::http::server::POST(std::string uri, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    this->set_handler("POST", std::move(uri), std::move(handler), std::move(get_mask), std::move(post_mask));
}

void manapi::net::http::server::OPTIONS(std::string uri, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    this->set_handler("OPTIONS", std::move(uri), std::move(handler), std::move(get_mask), std::move(post_mask));
}

void manapi::net::http::server::PUT(std::string uri, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    this->set_handler("PUT", std::move(uri), std::move(handler), std::move(get_mask), std::move(post_mask));
}

void manapi::net::http::server::PATCH(std::string uri, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    this->set_handler("PATCH", std::move(uri), std::move(handler), std::move(get_mask), std::move(post_mask));
}

// void manapi::net::http::server::DELETE(const std::string &uri, handler_template_t handler, const json_mask &get_mask, const json_mask &post_mask) {
//     this->set_handler("DELETE", uri, std::move(handler), get_mask, post_mask);
// }

void manapi::net::http::server::GET(std::string uri, std::string folder) {
    set_handler ("GET", std::move(uri), std::move(folder));
}

manapi::future<void> manapi::net::http::server::stop() {
    auto lk = co_await this->mx.lock_guard();

    if (server::stopping.exchange(true)) {
        co_return;
    }

    co_await this->ctx->eventloop()->unsubscribe_finish(std::exchange(this->event_id, 0));
    co_await this->stop_pool();
}

manapi::future<> manapi::net::http::server::_init_pool() {
    // init all pools
    if (this->config.contains("pools"))
    {
        for (auto it = this->config["pools"].begin<json::ARRAY>(); it != this->config["pools"].end<json::ARRAY>(); ++it, this->next_pool_id++)
        {
            auto p = std::make_unique<http_pool> (*it, this, this->next_pool_id, this->ctx->eventloop());
            co_await p->run();
            this->pools.insert({this->next_pool_id, std::move(p)});
        }
    }
}

manapi::future<void> manapi::net::http::server::_pool(const std::function<void()> &cb) {
    this->init_watcher = co_await this->ctx->eventloop()->watch_async([cb] (ev::async &w, int revents) -> void {
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
        co_await this->ctx->eventloop()->unwatch_async(this->init_watcher);
        this->init_watcher.reset();
    }
}