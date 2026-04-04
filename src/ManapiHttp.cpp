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
#include <regex>

#include "ManapiHttp.hpp"
#include "std/ManapiAsyncPromise.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "./include/ManapiSiteInternal.hpp"
#include "./include/ManapiUtils.hpp"

enum manapi_http_server_flags {
    MANAPI_HTTP_SERVER_FLAG_RUNNING = 1<<0,
    MANAPI_HTTP_SERVER_FLAG_STOPPING = 1<<1
};

manapi::net::http::server::~server() = default;

manapi::net::http::server::server(server &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::net::http::server & manapi::net::http::server::operator=(server &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::net::http::server::server(const server &n) = default;

manapi::net::http::server & manapi::net::http::server::operator=(const server &n) = default;

struct manapi::net::http::server::data2_t : manapi::net::http::site::data_t {
    pools_t pools;
    std::size_t next_pool_id;
    std::shared_ptr<ev::async> init_watcher;
    uint8_t flags;
};

manapi::net::http::server::server() : site() {

}

manapi::net::http::server::server(server_ctx sctx) : site() {
    auto data = std::make_shared<data2_t>();
    this->data = std::move(data);
    this->init_data_ (std::move(sctx));
    this->setup();
}

manapi::status_or<manapi::net::http::server> manapi::net::http::server::create(server_ctx sctx) MANAPIHTTP_NOEXCEPT {
    try {
        return server(std::move(sctx));
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return status_resource_exhausted();
    }
}

manapi::future<manapi::status> manapi::net::http::server::config(std::string path) {
    try {
        if (this->data->event_id)
            co_return status_already_exists("http:config already exists");

        this->data->event_id = async::current()->eventloop()->subscribe_finish(-1, [p = *this] () mutable
            -> future<> {
            auto res = co_await p.stop();
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http:Stop status=%.*s", res.msg().size(), res.msg().data());
        });

        this->data->clean_up_id = async::current()->eventloop()->subscribe_clean_up([p = *this] () mutable
            -> void {
            p.clean_up();
        });

        this->data->server_config = co_await this->data->sctx.storage().subscribe([p = *this] (auto &&f1)
            -> void { on_config_update(p.data, std::forward<decltype(f1)>(f1)); });

        auto res = co_await this->data->sctx.storage().edit_async (this->data->server_config,
            [this, path = std::move(path)] (manapi::json &config) mutable -> manapi::future<bool> {
                if (!config.is_object())
                    config = manapi::json::object();

                if (!config.contains("site") || !config["site"].is_object()) {

                    try {
                        auto exists = co_await manapi::fs::async_exists(path);
                        if (!exists.ok() || !exists.unwrap())
                        {
                            std::string data = manapi::json::object().dump(4);
                            auto res = co_await manapi::fs::async_write(path, std::move(data), ev::IRWXU, ev::FS_O_CREAT|ev::FS_O_TRUNC|ev::FS_O_WRONLY);
                            res.unwrap();
                        }

                        auto res = co_await manapi::fs::async_read (path);
                        auto obj = manapi::json(res.unwrap(), true);

                        if (!obj.is_object())
                            obj = manapi::json::object();

                        config["site"] = std::move(obj);
                    }
                    catch (std::exception const &e) {
                        MANAPIHTTP_LOG("server router: config read failed due to {}", e.what());
                    }

                    config["site_time"] = 0;
                    config["site_path"] = std::move(path);
                    config["cache_time"] = 0;

                    co_await this->setup_config(config);
                    *this->data->config_ = config;
                    co_return true;
                }
                *this->data->config_ = config;
                co_return false;
        });
        res.unwrap();
        co_return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "http:config() failed", e.what());
    }

    auto res = manapi::status_ok();
    try {
        res = co_await this->stop();
        this->clean_up();
    }
    catch (std::exception const &e) {
       res = manapi::status_unknown(std::string{e.what()});
    }

    if (!res.ok())
        manapi_log_ferror("http:failed to stop due to %s", res.msg().data());

    co_return status_internal("http:config() failed");
}

manapi::future<manapi::status> manapi::net::http::server::config_object(json config) {
    try {
        if (this->data->event_id)
            co_return status_already_exists("http:config already exists");

        this->data->event_id = async::current()->eventloop()->subscribe_finish(-1, [p = *this] () mutable
            -> future<> {
            auto res = co_await p.stop();
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http:Stop status=%.*s", res.msg().size(), res.msg().data());
        });

        this->data->clean_up_id = async::current()->eventloop()->subscribe_clean_up([p = *this] () mutable
            -> void {
            p.clean_up();
        });

        this->data->server_config = co_await this->data->sctx.storage().subscribe([data = this->data] (auto &&f1)
            -> void { on_config_update(data, std::forward<decltype(f1)>(f1)); });

        auto res = co_await this->data->sctx.storage().edit_async (this->data->server_config,
            [this, nconfig = std::move(config)] (manapi::json &config) mutable -> manapi::future<bool> {
                if (!config.is_object())
                    config = manapi::json::object();

                if (!config.contains("site") || !config["site"].is_object()) {

                    try {
                        if (!nconfig.is_object())
                            nconfig = manapi::json::object();

                        config["site"] = std::move(nconfig);
                    }
                    catch (std::exception const &e) {
                        MANAPIHTTP_LOG("server router: config read failed due to {}", e.what());
                    }
                }

                config["site_time"] = 0;
                config["site_path"] = "";
                config["cache_time"] = 0;

                co_await this->setup_config(config);
                co_return true;
        });
        res.unwrap();
        co_return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "http:config() failed", e.what());
    }

    auto res = manapi::status_ok();
    try {
        res = co_await this->stop();
        this->clean_up();
    }
    catch (std::exception const &e) {
        res = manapi::status_unknown(std::string{e.what()});
    }

    if (!res.ok())
        manapi_log_ferror("http:failed to stop due to %s", res.msg().data());

    co_return status_internal("http:config() failed");
}

manapi::future<manapi::status> manapi::net::http::server::start() {
    auto res = manapi::status_ok();
    auto data2 = std::static_pointer_cast<data2_t>(this->data);

    try {

        if (data2->flags & MANAPI_HTTP_SERVER_FLAG_STOPPING) {
            co_return status_unavailable("http:is stopping");
        }

        if (!this->data->event_id) {
            co_return status_unavailable("http:wasn't configured");
        }

        if (data2->flags & MANAPI_HTTP_SERVER_FLAG_RUNNING) {
            co_return status_already_exists("http:already running");
        }

        data2->flags |= MANAPI_HTTP_SERVER_FLAG_RUNNING;

        co_await this->init_pool_();

        typedef async::promise_sync<manapi::status> promise;
        res = co_await promise([this] (const promise::resolve_t& resolve, const promise::reject_t& reject) -> void {
            auto res = this->pool_([resolve] () mutable -> void {
                resolve (status_ok());
            });

            if (!res)
                resolve(std::move(res));
        });

        if (!res)
            goto err;

        co_return std::move(res);
    }
    catch (std::bad_alloc const &) {
        res = status_resource_exhausted();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "start() failed", e.what());
        res = status_internal("start() failed");
    }
err:
    data2->flags ^= MANAPI_HTTP_SERVER_FLAG_RUNNING;
    co_return std::move(res);
}

manapi::status manapi::net::http::server::GET(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("GET", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::status manapi::net::http::server::POST(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("POST", std::move(uri), std::move(handler),  std::move(params)).err();
}

manapi::status manapi::net::http::server::OPTIONS(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("OPTIONS", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::status manapi::net::http::server::PUT(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("PUT", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::status manapi::net::http::server::PATCH(std::string uri, handler_template_t handler, manapi::json params) MANAPIHTTP_NOEXCEPT {
    return this->handler("PATCH", std::move(uri), std::move(handler), std::move(params)).err();
}

manapi::status manapi::net::http::server::GET(std::string uri, std::string folder, handler_template_t handler) MANAPIHTTP_NOEXCEPT {
    return this->handler ("GET", std::move(uri), std::move(folder), std::move(handler)).err();
}

manapi::future<manapi::status> manapi::net::http::server::stop() {
    auto res = manapi::status_ok();
    auto data = std::static_pointer_cast<data2_t>(this->data);
    try {

        if ((data->flags & MANAPI_HTTP_SERVER_FLAG_STOPPING)) {
            co_return status_already_exists("http:already stopping");
        }

        if (!this->data->event_id) {
            co_return status_not_found("http:wasn't configured");
        }

        data->flags |= MANAPI_HTTP_SERVER_FLAG_STOPPING;

        // короч. мне лень. это проблема не сегодняшнего меня


        co_await stop_pool(data);

        if (data->server_config) {
            try {
                manapi::unwrap(co_await data->sctx.storage().edit_async(data->server_config, [t = data] (json &data) -> manapi::future<bool> {
                    if (!data.contains("saved") || data["saved"] != true) {
                        data["saved"] = true;
                        if (data.contains("site_path")
                            && data["site"].contains("save_config")
                            && data["site"]["save_config"] == true)
                            co_await save_config(t);

                        // cache config
                        co_await manapi::fs::async_write(fs::path::join(data["cache_path"].as_string(), std::string{site::default_config_name}),
                            data["cache"].dump(),
                            ev::IRWXU, ev::FS_O_CREAT|ev::FS_O_TRUNC|ev::FS_O_WRONLY);
                    }

                    co_return false;
                }));
            }
            catch (std::exception const &e) {
                async::current()->logger()->ferror(ERR_UNKNOWN,
                    "http: couldn't save the configuration file due to %s", e.what());
            }

            co_await data->sctx.storage().unsubscribe(std::move(data->server_config));
        }

        if (data->init_watcher) {
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http:unwatch_async(this->data->init_watcher)");
            async::current()->eventloop()->stop_watcher(std::move(data->init_watcher));
            manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http:finish unwatch_async(this->data->init_watcher)");
        }


        this->clean_up();

        if (data->flags & MANAPI_HTTP_SERVER_FLAG_RUNNING)
            data->flags ^= MANAPI_HTTP_SERVER_FLAG_RUNNING;

        async::current()->eventloop()->unsubscribe_finish(std::exchange(data->event_id, 0));
        async::current()->eventloop()->unsubscribe_clean_up(std::exchange(data->clean_up_id, 0));

        co_return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "stop() failed", e.what());
        res = status_internal("stop() failed");
    }
err:
    data->flags ^= MANAPI_HTTP_SERVER_FLAG_STOPPING;
    co_return std::move(res);
}

std::shared_ptr<manapi::net::http::site> manapi::net::http::server::copy() {
    return std::make_shared<manapi::net::http::server>(*this);
}

manapi::future<> manapi::net::http::server::init_pool_() {
    auto const data2 = std::static_pointer_cast<data2_t>(this->data);
    auto &pool = data2->pools[std::this_thread::get_id()];
    // init all pools
    if (data2->config_->at("site").contains("pools"))
    {
        auto bb = data2->config_;
        auto &pools = bb->at("site")["pools"];

        {
            auto &pools_data = data2->server_config->as<server_ctx::worker_data_t>()->pools;
            while (pools_data.size() < pools.size()) {
                pools_data.push_back(server_ctx::pool_t({},
                    std::make_unique<std::mutex>()));
            }
        }

        for (auto it = pools.begin<json::ARRAY>(); it != pools.end<json::ARRAY>(); ++it, data2->next_pool_id++)
        {
            std::unique_ptr<http_pool> p;

            try {
                p = std::make_unique<http_pool> (*it, data2->server_config, this->copy(), data2->next_pool_id);
                auto res = co_await p->run();
                if (res.ok())
                    pool.insert({data2->next_pool_id, std::move(p)});
                else
                    res.log();
            }
            catch (std::exception const &e) {
                manapi::async::current()->logger()->error(ERR_FAILED_PRECONDITION, "init pool failed due to {}", e.what());
            }

            if (p) {
                auto err = co_await p->stop();
                err.log();
            }
        }
    }
}

manapi::status manapi::net::http::server::pool_(std::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto const data2 = std::static_pointer_cast<data2_t>(this->data);
        auto wres = async::current()->eventloop()->create_watcher_async(
            [data2, cb = std::move(cb)] (const std::shared_ptr<ev::async> &w) mutable
            -> void {
            cb();

            auto wz = std::move(data2->init_watcher);
            manapi::async::current()->eventloop()->stop_watcher(std::move(wz));
        });

        if (!wres)
            return manapi::status{wres.code(), wres.status_msg()};

        data2->init_watcher = wres.unwrap();

        if (auto rhs = data2->init_watcher->send())
            manapi_log_error("%s due to %s", "http:Failed", ev::strerror(rhs));

        return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "http:Failed", e.what());
        return status_internal("http:Failed");
    }
}

void manapi::net::http::server::clean_up() {
    auto data2 = std::static_pointer_cast<data2_t>(this->data);
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
