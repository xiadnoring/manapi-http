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

manapi::net::http::server::server(server_ctx sctx)
        : site(std::move(sctx)) {
    this->data2 = std::make_shared<data2_t>(std::make_unique<async::mutex>(), true, pools_t(), 0UL, 0UL, 0UL, nullptr, nullptr);
    this->setup ();
}

manapi::future<void> manapi::net::http::server::start() {
    auto lk = co_await this->data2->mx->lock_guard();

    if (!this->data2->stopping.exchange(false)) {
        co_return;
    }

    this->data2->event_id = async::current()->eventloop()->subscribe_finish([data = this->data, data2 = this->data2] ()
        -> future<> {
        co_return co_await stop_(data, data2, true);
    });

    this->data2->clean_up_id = async::current()->eventloop()->subscribe_clean_up([data2 = this->data2] ()
        -> void {
        clean_up(data2);
    });

    co_await this->init_pool_();

    using promise = async::promise<void, std::false_type>;
    co_await  promise([this, &lk] (promise::resolve_t resolve, promise::reject_t reject) -> void {
        this->pool_([&lk, resolve = std::move(resolve)] () mutable -> void {
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

void manapi::net::http::server::GET(std::string uri, std::string folder, handler_template_t handler, json_mask get_mask, json_mask post_mask) {
    this->handler ("GET", std::move(uri), std::move(folder), std::move(handler), std::move(get_mask), std::move(post_mask));
}

manapi::future<void> manapi::net::http::server::stop() {
    return this->stop_(this->data, this->data2, false);
}

manapi::future<void> manapi::net::http::server::stop_(std::shared_ptr<site::data_t> data, std::shared_ptr<data2_t> data2, bool evloop) {
    auto lk = co_await data2->mx->lock_guard();

    if (data2->stopping.exchange(true)) {
        co_return;
    }

    if (!evloop) {
        async::current()->eventloop()->unsubscribe_finish(std::exchange(data2->event_id, 0));
        async::current()->eventloop()->unsubscribe_clean_up(std::exchange(data2->clean_up_id, 0));
    }

    co_await stop_pool(data2);

    try {
        if (!(data->server_config->flags.fetch_or(0b1) & 0b1)) {
            auto lkc = co_await data->server_config->cache_mx->lock_guard();

            co_await save_config(data);

            auto const cconfig = data->server_config->cache;
            lkc.call();

            // cache config
            co_await manapi::filesystem::async_write(data->config_cache_dir + site::default_config_name, cconfig.dump(),
                ev::IRWXU, ev::FS_O_CREAT|ev::FS_O_TRUNC|ev::FS_O_WRONLY);
        }
    }
    catch (std::exception const &e) {
        async::current()->logger()->error(manapi::logger::default_service, ERR_CONFIG_ERROR, "http: couldn't save the configuration file due to {}", e.what());
    }

    if (data2->init_watcher) {
        printf("unwatch_async(this->data2->init_watcher);\n");
        async::current()->eventloop()->stop_watcher(std::move(data2->init_watcher));
        printf("finish unwatch_async(this->data2->init_watcher);\n");
    }

    if (data->server_config_notifier) {
        data->sctx.remove_server_sub(data->server_config_notifier);
        async::current()->eventloop()->stop_watcher(std::move(data->server_config_notifier));
    }

    if (!evloop) {
        /* clean up */
        clean_up(data2);
    }
}

manapi::future<> manapi::net::http::server::init_pool_() {
    auto &pool = this->data2->pools[std::this_thread::get_id()];
    // init all pools
    if (this->data->config_->contains("pools"))
    {
        auto bb = this->data->config_;
        auto &pools = bb->at("pools");
        for (auto it = pools.begin<json::ARRAY>(); it != pools.end<json::ARRAY>(); ++it, this->data2->next_pool_id++)
        {
            std::unique_ptr<http_pool> p;

            try {
                auto worker_data = this->data->sctx.worker_config(this->data2->next_pool_id);
                p = std::make_unique<http_pool> (*it, std::move(worker_data), *this, this->data2->next_pool_id, async::current()->eventloop());
                co_await p->run();
                pool.insert({this->data2->next_pool_id, std::move(p)});
            }
            catch (std::exception const &e) {
                manapi::async::current()->logger()->error(manapi::logger::default_service, ERR_SOCKET, "init pool failed due to {}", e.what());
            }

            if (p) {
                co_await p->stop();
            }
        }
    }
}

manapi::future<> manapi::net::http::server::call_in_thread_(const async::shared_cthread &thr, std::move_only_function<manapi::future<>()> cb) {
    using promise = manapi::async::promise<void>;
    co_await promise ([&] (promise::resolve_t resolve, promise::reject_t reject) -> manapi::future<> {
        auto w = async::current()->eventloop()->create_watcher_async([resolve = std::move(resolve)] (ev::shared_async &w) mutable
            -> void {
            auto cb = std::move(resolve);
            async::current()->eventloop()->stop_watcher(w);
            /* -- [ DELETED ] -- */
            cb();
        });

        /* run the callback in other event loop */
        co_await thr->eventloop()->custom_callback([this, cb = std::move(cb), w = std::move(w)] (event_loop *ev) mutable
            -> void {
            manapi::async::run(std::move(cb), [w = std::move(w)] (std::exception_ptr err) -> void {
                    if (err) {
                        /* error */
                    }
                    w->send();
                });
        });
    });
}

void manapi::net::http::server::pool_(std::move_only_function<void()> cb) {
    this->data2->init_watcher = async::current()->eventloop()->create_watcher_async([data2 = this->data2, cb = std::move(cb)] (std::shared_ptr<ev::async> &w) mutable
        -> void {
        cb();

        auto wz = std::move(data2->init_watcher);
        manapi::async::current()->eventloop()->stop_watcher(std::move(wz));
    });

    this->data2->init_watcher->send();
}

void manapi::net::http::server::clean_up(std::shared_ptr<data2_t> data2) {
    // clean
    data2->pools.clear();
    // reset
    data2->next_pool_id = 0;
}

manapi::future<> manapi::net::http::server::stop_pool(std::shared_ptr<data2_t> data2) {
    auto &pools = data2->pools[std::this_thread::get_id()];
    MANAPIHTTP_LOG2("cv_stopping -> pass");

    // stop all pools
    for (const auto &pool: pools)
    {
        MANAPIHTTP_LOG ("pool #{} is stopping...", pool.first);
        co_await pool.second->stop();
        MANAPIHTTP_LOG ("pool #{} stopped successfully", pool.first);
    }

    std::cout << "7878\n";
    MANAPIHTTP_LOG2("7878 pools(...) -> pass");
}
