#include <iostream>
#include <csignal>
#include <utility>
#include <vector>
#include <memory.h>
#if defined(__unix__)||defined(__APPLE__)
#   include <arpa/inet.h>
#   include <netdb.h>
#endif
#include <filesystem>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <fcntl.h>

#include "ManapiHttpPool.hpp"
#include "services/ManapiTaskFunction.hpp"
#include "http/HTTPv1_1.hpp"
#include <http/HTTPv2.hpp>

#include "http/HeaderView.hpp"

manapi::net::http_pool::http_pool(const json &config, class site *site, const size_t &id, std::shared_ptr<event_loop> events) {
    this->events = std::move(events);
    this->config = std::make_shared <http::config> (site->async_context(), config);
    this->id = id;
    this->site = site;
    this->mx = std::make_shared<async::mutex>(this->site->async_context());

    this->config->function_contains_compressor([site] (const std::string &name) -> bool {
        return site->contains_compressor_for_file(name)
            || site->contains_compressor_for_string(name);
    });
}

manapi::net::http_pool::~http_pool() = default;

manapi::future<> manapi::net::http_pool::stop() {
    auto lk = co_await this->mx->lock_guard();
    MANAPIHTTP_LOG(this->site->async_context(), "{}", "shutdown socket");

    // stop watcher
    co_await this->site->async_context()->eventloop()->custom_callback([this] (event_loop *ev)
        -> void {
        this->worker->stop();
    });
}

manapi::future<void> manapi::net::http_pool::run() {
    return this->_pool();
}

manapi::future<void> manapi::net::http_pool::_pool() {
    MANAPIHTTP_LOG(this->site->async_context(), "pool init #{}", this->id);

    auto lk = co_await this->mx->lock_guard();

    MANAPIHTTP_LOG(this->site->async_context(), "pool start #{}", this->id);

    co_await this->events->custom_callback([&] (event_loop *ev) -> void {
        {
            auto implementation = this->config->implementation();
            auto transport = this->config->transport();
            auto implementations = this->site->transport_protocol_worker(*transport);

            if (implementations.contains(*implementation))
            {
                auto generate = implementations[*implementation];
                this->worker = generate (this->config);
                this->worker->watcher = nullptr;
                this->worker->le = this->events;
                this->worker->worker = std::weak_ptr<worker::base> (this->worker);
                this->worker->init();
            }
            else
            {
                std::string available;
                if (!implementations.empty()) {
                    auto it = implementations.begin();
                    goto skip;
                    for (; it != implementations.end(); ++it) {
                        available += ',';
                        skip:
                        available += it->first;
                    }
                }
                MANAPIHTTP_LOG(this->site->async_context(), "implementation by {} not found in {}. Available: [{}]", *implementation, *transport, available);
                THROW_MANAPIHTTP_EXCEPTION2(ERR_CONFIG_ERROR, "implementation not found");
            }
        }
    });
}

manapi::net::site & manapi::net::http_pool::get_site() const {
    return *site;
}
