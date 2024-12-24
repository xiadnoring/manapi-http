#include <iostream>
#include <csignal>
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

#include "ManapiHttpPool.hpp"
#include "services/ManapiTaskFunction.hpp"
#include "http/HTTPv1_1.hpp"
#include <http/HTTPv2.hpp>

#include "http/HeaderView.hpp"

manapi::net::http_pool::http_pool(const json &config, class site *site, const size_t &id, ev::loop_ref loop) : loop(std::exchange(loop, nullptr)) {
    this->config = std::make_shared <http::config> (config);
    this->id = id;
    this->site = site;

    this->config->set_function_contains_compressor([site] (const std::string &name) -> bool {
        return site->contains_compressor(name);
    });
}

manapi::net::http_pool::~http_pool() = default;

ev::loop_ref manapi::net::http_pool::get_loop() {
    return loop;
}

void manapi::net::http_pool::stop() {
    std::lock_guard<std::mutex> lk (this->mx);
    MANAPIHTTP_LOG("{}", "shutdown socket");

    // close socket
    shutdown(config->get_socket_fd(), SHUT_RDWR);

    // stop watcher
    watcher->stop();
}

void manapi::net::http_pool::run() {
    this->_pool();
}

int manapi::net::http_pool::_pool() {
    MANAPIHTTP_LOG("pool init #{}", id);

    std::unique_lock <std::mutex> lock (mx);

    MANAPIHTTP_LOG("pool start #{}", id);


    this->watcher = std::make_shared <ev::io> (this->loop);
    this->async_watcher = std::make_shared<ev::async>(this->loop);

    {
        auto implementation = config->get_implementation();
        auto transport = config->get_transport();
        auto implementations = site->get_transport_protocol_worker(*transport);

        if (implementations.contains(*implementation))
        {
            auto generate = implementations[*implementation];
            this->worker = generate (this->config);
            this->worker->init();
            this->worker->loop = this->loop;
            this->worker->watcher = this->watcher;
            this->worker->worker = std::weak_ptr<worker::base> (this->worker);
            this->worker->async_watcher = this->async_watcher;
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
            THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "implementation by {} not found in {}. Available: [{}]", *implementation, *transport, available);
        }
    }

    this->async_watcher->set<worker::base, &worker::base::onasync>(this->worker.get());
    this->watcher->set <worker::base, &worker::base::onrecv> (this->worker.get());

    this->async_watcher->start();
    this->watcher->start(config->get_socket_fd(), ev::READ);

    return 0;
}

manapi::net::site & manapi::net::http_pool::get_site() const {
    return *site;
}
