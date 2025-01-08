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

std::atomic<bool> manapi::net::http::server::stopped_interrupt = false;

manapi::net::Atomic<std::set <manapi::net::http::server *>> manapi::net::http::server::running;

void handler_interrupt (int sig)
{
    manapi::net::http::server::stop_all_servers();

    if (sig == SIGFPE)
    {
        exit (sig);
    }
}

manapi::net::http::server::~server() = default;

manapi::net::http::server::server(std::shared_ptr<threadpool<task>> taskpool, std::shared_ptr<manapi::timerpool> timerpool) : site(taskpool, std::move(timerpool)), mx(taskpool) {
    server::signal_init();
    this->stopping.store(false);
    setup ();
}

manapi::future<void> manapi::net::http::server::pool() {
    if (server::stopping) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "Application was stopped");
    }

    auto lk = co_await this->mx.lock_guard();

    server::running.update([this] (auto &v) {
        v.insert(this);
    });

    // init all pools
    if (this->config.contains("pools"))
    {
        for (auto it = this->config["pools"].begin<json::ARRAY>(); it != this->config["pools"].end<json::ARRAY>(); ++it, this->next_pool_id++)
        {
            auto p = std::make_unique<http_pool> (*it, this, this->next_pool_id, this->loop);
            p->run();
            this->pools.insert({this->next_pool_id, std::move(p)});
        }
    }

    co_await async::promise<void> (this->taskpool, [this, &lk] (async::promise<void>::resolve_ref_t resolve, async::promise<void>::reject_ref_t reject) -> void {
        this->taskpool->append_task([this, &lk, resolve] () -> void {
            this->stop_watcher = std::make_shared<ev::async>(this->loop);
            this->stop_watcher->set<server, &server::_async_break_loop> (this);
            this->stop_watcher->start();

            this->adding_watcher_async = std::make_unique<ev::async>(this->loop);
            this->adding_watcher_async->set<server, &server::custom_watcher_fd_async>(this);
            this->adding_watcher_async->start();

            auto init_watcher = this->create_watcher_async([&lk] (ev::async &w, int revents) -> void {
                w.stop();

                /* init has been finished */
                lk.call();
            });

            init_watcher->start();
            init_watcher->send();

            this->loop.run(ev::AUTO);

            this->adding_watcher_async->stop();
            this->stop_watcher->stop();

            resolve ();
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

    if (!this->stopping.exchange(true)) {
        auto promise = async::promise<void> (this->taskpool,
            [this] (async::promise<void>::resolve_ref_t resolve, async::promise<void>::reject_ref_t reject) -> void {
            this->resolve_stop = resolve;
        });
        this->stop_watcher->send();
        co_await promise;
    }
}

void manapi::net::http::server::stop_all_servers() {
    manapi::net::http::server::stopped_interrupt.store(true);

    while (!server::running.get()->empty())
    {
        auto it = *server::running.get()->begin();
        it->stop().get(it->taskpool);
    }
}

void manapi::net::http::server::signal_init() {
    signal (SIGPIPE, SIG_IGN);
    signal (SIGABRT, handler_interrupt);
    signal (SIGKILL, handler_interrupt);
    signal (SIGTERM, handler_interrupt);
    signal (SIGSTOP, handler_interrupt);
}

void manapi::net::http::server::custom_watcher_fd_async(ev::async &w, int revents) {
    site::custom_watcher_fd_async(w, revents);
}

void manapi::net::http::server::stop_pool(async::promise<void>::resolve_t resolve) {
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

    std::thread ([this, resolve = std::move(resolve)] () mutable -> void {
        this->save();

        server::running.update([this] (auto &v) -> void {
             v.erase(this);
        });

        MANAPIHTTP_LOG2("all tasks are closed");

        // reset
        this->next_pool_id = 0;
        this->stopping.store(false);

        resolve();
    }).detach();
}

void manapi::net::http::server::_async_break_loop(ev::async &watcher, int revents) {
    this->loop.break_loop();
    this->stop_pool(std::exchange(this->resolve_stop, nullptr));
}
