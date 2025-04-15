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

manapi::net::http::server::server(server &&n) noexcept : site(n) {
    this->data2 = std::move(n.data2);
}

manapi::net::http::server & manapi::net::http::server::operator=(server &&n) noexcept {
    this->data = std::move(n.data);
    this->data2 = std::move(n.data2);
    return *this;
}

manapi::net::http::server::server(const server &n) : site(n) {
    this->data2 = n.data2;
}

manapi::net::http::server & manapi::net::http::server::operator=(const server &n) {
    this->data = n.data;
    this->data2 = n.data2;
    return *this;
}

manapi::net::http::server::server(const std::shared_ptr<manapi::async::context> &ctx)
        : site(ctx) {
    this->data2 = std::make_shared<data2_t>(ctx, true, std::map<size_t, std::unique_ptr<http_pool>>(), 0UL, 0UL, 0UL, nullptr, nullptr);
    this->setup ();
}

manapi::future<void> manapi::net::http::server::start() {
    // initialization object pools
    this->bufferpool()->init(0);

    auto lk = co_await this->data2->mx.lock_guard();

    if (!this->data2->stopping.exchange(false)) {
        co_return;
    }

    co_await this->_init_pool();

    this->data2->event_id = co_await this->data->ctx->eventloop()->subscribe_finish([this] ()
        -> future<> { co_return co_await this->stop_(true); });

    this->data2->clean_up_id = co_await this->data->ctx->eventloop()->subscribe_clean_up([this] ()
        -> void { this->clean_up(); });

    co_await async::promise<void> (this->data->ctx, [this, &lk] (async::promise<void>::resolve_ref_t resolve, async::promise<void>::reject_ref_t reject) -> future<> {
        co_await this->_pool([&lk, resolve] () mutable -> void {
            lk.call();
            resolve ();
        });
    });
}

void manapi::net::http::server::GET(std::string uri, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    this->handler("GET", std::move(uri), std::move(handler), std::move(get_mask), std::move(post_mask));
}

void manapi::net::http::server::POST(std::string uri, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    this->handler("POST", std::move(uri), std::move(handler), std::move(get_mask), std::move(post_mask));
}

void manapi::net::http::server::OPTIONS(std::string uri, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    this->handler("OPTIONS", std::move(uri), std::move(handler), std::move(get_mask), std::move(post_mask));
}

void manapi::net::http::server::PUT(std::string uri, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    this->handler("PUT", std::move(uri), std::move(handler), std::move(get_mask), std::move(post_mask));
}

void manapi::net::http::server::PATCH(std::string uri, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    this->handler("PATCH", std::move(uri), std::move(handler), std::move(get_mask), std::move(post_mask));
}

// void manapi::net::http::server::DELETE(const std::string &uri, handler_template_t handler, const json_mask &get_mask, const json_mask &post_mask) {
//     this->set_handler("DELETE", uri, std::move(handler), get_mask, post_mask);
// }

void manapi::net::http::server::GET(std::string uri, std::string folder) {
    handler ("GET", std::move(uri), std::move(folder));
}

manapi::future<void> manapi::net::http::server::stop() {
    return this->stop_(false);
}

manapi::future<void> manapi::net::http::server::stop_(bool evloop) {
    auto lk = co_await this->data2->mx.lock_guard();

    if (this->data2->stopping.exchange(true)) {
        co_return;
    }

    if (!evloop) {
        co_await this->data->ctx->eventloop()->unsubscribe_finish(std::exchange(this->data2->event_id, 0));
        co_await this->data->ctx->eventloop()->unsubscribe_clean_up(std::exchange(this->data2->clean_up_id, 0));
    }

    co_await this->stop_pool();

    if (!evloop) {
        /* clean up */
        this->clean_up();
    }
}

manapi::future<> manapi::net::http::server::_init_pool() {
    // init all pools
    if (this->data->config_.contains("pools"))
    {
        for (auto it = this->data->config_["pools"].begin<json::ARRAY>(); it != this->data->config_["pools"].end<json::ARRAY>(); ++it, this->data2->next_pool_id++)
        {
            auto p = std::make_unique<http_pool> (*it, this, this->data2->next_pool_id, this->data->ctx->eventloop());
            co_await p->run();
            this->data2->pools.insert({this->data2->next_pool_id, std::move(p)});
        }
    }
}

manapi::future<void> manapi::net::http::server::_pool(const std::function<void()> &cb) {
    this->data2->init_watcher = co_await this->data->ctx->eventloop()->watch_async([cb] (ev::async &w, int revents) -> void {
        w.stop();
        cb();
    });

    this->data2->init_watcher->send();
}

void manapi::net::http::server::clean_up() {
    // clean
    this->data2->pools.clear();
    // reset
    this->data2->next_pool_id = 0;
}

manapi::future<> manapi::net::http::server::stop_pool() {
    MANAPIHTTP_LOG2(this->async_context(), "cv_stopping -> pass");

    // stop all pools
    for (const auto &pool: this->data2->pools)
    {
        MANAPIHTTP_LOG (this->async_context(), "pool #{} is stopping...", pool.first);
        pool.second->stop();
        MANAPIHTTP_LOG (this->async_context(), "pool #{} stopped successfully", pool.first);
    }

    MANAPIHTTP_LOG2(this->async_context(), "pools(...) -> pass");

    this->save();

    if (this->data2->init_watcher) {
        printf("unwatch_async(this->data2->init_watcher);\n");
        co_await this->data->ctx->eventloop()->unwatch_async(this->data2->init_watcher);
        this->data2->init_watcher.reset();
        printf("finish unwatch_async(this->data2->init_watcher);\n");
    }
}
