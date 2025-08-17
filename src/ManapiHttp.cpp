#include <iostream>
#include <csignal>
#include <memory>
#include <utility>
#include <vector>
#include <memory.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>

#include "ManapiHttp.hpp"
#include "std/ManapiAsyncPromise.hpp"

#include "./include/ManapiSiteInternal.hpp"
#include "./include/ManapiUtils.hpp"
#include "./include/ManapiUtils.hpp"

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

struct manapi::net::http::server::data2_t {
    std::unique_ptr<async::mutex> mx;
    std::atomic <bool> stopping;
    pools_t pools;
    std::size_t event_id;
    std::size_t clean_up_id;
    std::size_t next_pool_id;
    async::promise_sync<void>::resolve_t resolve_stop;
    std::shared_ptr<ev::async> init_watcher;
};

manapi::net::http::server::server(server_ctx sctx)
        : site(std::move(sctx)) {
    this->data2 = std::make_shared<data2_t>(std::make_unique<async::mutex>(), true, pools_t(), 0UL, 0UL, 0UL, nullptr, nullptr);
    this->setup ();
}

manapi::error::status_or<manapi::net::http::server> manapi::net::http::server::create(server_ctx sctx) MANAPIHTTP_NOEXCEPT {
    try {
        return server(std::move(sctx));
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return error::status_resource_exhausted();
    }
}

manapi::future<manapi::error::status> manapi::net::http::server::start() {
    try {
        auto lk = co_await this->data2->mx->lock_guard();

        if (!this->data2->stopping.exchange(false)) {
            co_return error::status_already_exists("already running");
        }

        this->data2->event_id = async::current()->eventloop()->subscribe_finish([data = this->data, data2 = this->data2] ()
            -> future<> {
            auto res = co_await stop_(data, data2, true);
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http:Stop status=%.*s", res.msg().size(), res.msg().data());
        });

        this->data2->clean_up_id = async::current()->eventloop()->subscribe_clean_up([data2 = this->data2] ()
            -> void {
            clean_up(data2);
        });

        co_await this->init_pool_();

        using promise = async::promise_sync<error::status>;
        co_return co_await promise([this, &lk] (promise::resolve_t resolve, promise::reject_t reject) -> void {
            auto res = this->pool_([&lk, resolve] () mutable -> void {
                lk.call();
                resolve (error::status_ok());
            });

            if (!res)
                resolve(std::move(res));
        });
    }
    catch (std::bad_alloc const &) {
        co_return error::status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "start() failed", e.what());
    }
    co_return error::status_internal("start() failed");
}

manapi::error::status manapi::net::http::server::GET(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("GET", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::error::status manapi::net::http::server::POST(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("POST", std::move(uri), std::move(handler),  std::move(params)).err();
}

manapi::error::status manapi::net::http::server::OPTIONS(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("OPTIONS", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::error::status manapi::net::http::server::PUT(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("PUT", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::error::status manapi::net::http::server::PATCH(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("PATCH", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::error::status manapi::net::http::server::GET(std::string uri, std::string folder, handler_template_t handler) MANAPIHTTP_NOEXCEPT {
    return this->handler ("GET", std::move(uri), std::move(folder), std::move(handler)).err();
}

manapi::future<manapi::error::status> manapi::net::http::server::stop() {
    co_return co_await this->stop_(this->data, this->data2, false);
}

manapi::future<manapi::error::status> manapi::net::http::server::stop_(std::shared_ptr<site::data_t> data, std::shared_ptr<data2_t> data2, bool evloop) {
    try {
        auto lk = co_await data2->mx->lock_guard();

        if (data2->stopping.exchange(true)) {
            co_return error::status_not_found("not exists");
        }

        if (!evloop) {
            async::current()->eventloop()->unsubscribe_finish(std::exchange(data2->event_id, 0));
            async::current()->eventloop()->unsubscribe_clean_up(std::exchange(data2->clean_up_id, 0));
        }

        co_await stop_pool(data2);

        try {
            co_await data->sctx.storage().edit_async(data->server_config, [t = data] (json &data) -> manapi::future<bool> {
                if (!data.contains("saved") || data["saved"] != true) {
                    data["saved"] = true;
                    if (data.contains("site_path")
                        && data["site"].contains("save_config")
                        && data["site"]["save_config"] == true)
                        co_await save_config(t);

                    // cache config
                    co_await manapi::filesystem::async_write(filesystem::path::join(data["cache_path"].as_string(), std::string{site::default_config_name}),
                        data["cache"].dump(),
                        ev::IRWXU, ev::FS_O_CREAT|ev::FS_O_TRUNC|ev::FS_O_WRONLY);
                }

                co_return false;
            });
        }
        catch (std::exception const &e) {
            async::current()->logger()->error(manapi::logger::default_service, ERR_FAILED_PRECONDITION, "http: couldn't save the configuration file due to {}", e.what());
        }

        if (data2->init_watcher) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http:unwatch_async(this->data2->init_watcher)");
            async::current()->eventloop()->stop_watcher(std::move(data2->init_watcher));
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http:finish unwatch_async(this->data2->init_watcher)");
        }

        if (data->server_config) {
            co_await data->sctx.storage().unsubscribe(data->server_config);
            data->server_config.reset();
        }

        if (!evloop) {
            /* clean up */
            clean_up(data2);
        }
        co_return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "stop() failed", e.what());
    }
    co_return error::status_internal("stop() failed");
}

manapi::future<> manapi::net::http::server::init_pool_() {
    auto &pool = this->data2->pools[std::this_thread::get_id()];
    // init all pools
    if (this->data->config_->at("site").contains("pools"))
    {
        auto bb = this->data->config_;
        auto &pools = bb->at("site")["pools"];

        {
            auto &pools_data = this->data->server_config->as<server_ctx::worker_data_t>()->pools;
            while (pools_data.size() < pools.size()) {
                pools_data.push_back(server_ctx::pool_t({},
                    std::make_unique<std::mutex>()));
            }
        }

        for (auto it = pools.begin<json::ARRAY>(); it != pools.end<json::ARRAY>(); ++it, this->data2->next_pool_id++)
        {
            std::unique_ptr<http_pool> p;

            try {
                p = std::make_unique<http_pool> (*it, this->data->server_config, *this, this->data2->next_pool_id, async::current()->eventloop());
                auto res = co_await p->run();
                if (res.ok())
                    pool.insert({this->data2->next_pool_id, std::move(p)});
                else
                    res.log();
            }
            catch (std::exception const &e) {
                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_FAILED_PRECONDITION, "init pool failed due to {}", e.what());
            }

            if (p) {
                auto err = co_await p->stop();
                err.log();
            }
        }
    }
}

manapi::error::status manapi::net::http::server::pool_(std::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto wres = async::current()->eventloop()->create_watcher_async(
            [data2 = this->data2, cb = std::move(cb)] (const std::shared_ptr<ev::async> &w) mutable
            -> void {
            cb();

            auto wz = std::move(data2->init_watcher);
            manapi::async::current()->eventloop()->stop_watcher(std::move(wz));
        });

        if (!wres)
            return error::status{wres.err()};

        this->data2->init_watcher = wres.unwrap();

        if (auto rhs = this->data2->init_watcher->send())
            manapi_log_error("%s due to %s", "http:Failed", ev::strerror(rhs));

        return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "http:Failed", e.what());
        return error::status_internal("http:Failed");
    }
}

void manapi::net::http::server::clean_up(std::shared_ptr<data2_t> data2) {
    // clean
    data2->pools.clear();
    // reset
    data2->next_pool_id = 0;
}

manapi::future<> manapi::net::http::server::stop_pool(std::shared_ptr<data2_t> data2) {
    auto &pools = data2->pools[std::this_thread::get_id()];

    // stop all pools
    for (const auto &pool: pools)
    {
        auto config = pool.second->config();
        manapi_log_trace (manapi::debug::LOG_TRACE_HIGH, "pool %.*s:%.*s #%zu is stopping...",
            config->address.size(), config->address.data(), config->port.size(), config->port.data(), pool.first);
        auto res= co_await pool.second->stop();
        if (!res.ok())
            res.log();
        manapi_log_trace (manapi::debug::LOG_TRACE_HIGH, "pool #%zu stopped successfully", pool.first);
    }
}
