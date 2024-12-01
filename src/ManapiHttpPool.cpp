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
#include "ManapiTaskHttp.hpp"
#include "ManapiTaskFunction.hpp"
#include "http/HTTPv1_1.hpp"
#include <http/HTTPv2.hpp>

#include "http/HeaderView.hpp"

manapi::net::http_pool::http_pool(const json &config, class site *site, const size_t &id) {
    this->config = std::make_shared <http::config> (config);
    this->id = id;
    this->site = site;

    this->config->set_function_contains_compressor([site] (const std::string &name) -> bool { return site->contains_compressor(name); });
}

manapi::net::http_pool::~http_pool() = default;

ev::loop_ref manapi::net::http_pool::get_loop() {
    return loop;
}

void manapi::net::http_pool::stop() {
    std::lock_guard <std::mutex> lock (m_initing);

    MANAPIHTTP_LOG("{}", "shutdown socket");

    // close socket
    shutdown(config->get_socket_fd(), SHUT_RDWR);

    // stop watcher
    ev_io->stop();

    // break loop
    loop.break_loop();

    // wait while loop is running
    std::lock_guard<std::mutex> lk (m_running);
}

std::future <int> manapi::net::http_pool::run() {
    // wait until another loop is stopping
    m_running.lock();

    pool_promise = std::make_unique<std::promise <int> > ();

    std::thread t ([this] () {
        utils::before_delete unwrap_pool_promise ([this] () -> void {
            pool_promise = nullptr;

            m_running.unlock();
        });

        pool_promise->set_value(_pool());
    });

    t.detach();

    return pool_promise->get_future();
}

const int &manapi::net::http_pool::get_fd() {
    return config->get_socket_fd();
}

int manapi::net::http_pool::_pool() {
    MANAPIHTTP_LOG("pool init #{}", id);

    std::unique_lock <std::mutex> lock (m_initing);

    MANAPIHTTP_LOG("pool start #{}", id);

    ev_io  = std::make_unique<ev::io> (loop);

    auto implementations = site->get_transport_protocol_worker(config->get_transport());

    if (implementations.contains(config->get_implementation()))
    {
        auto generate = implementations[config->get_implementation()];
        worker = generate (config);
        worker->init();
    }
    else
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "implementation by {} not found in {}", config->get_implementation(), config->get_transport());
    }

    // create watcher
    ev_io->set <http_pool, &http_pool::new_connection> (this);
    ev_io->start(config->get_socket_fd(), ev::READ);

    // say, that it can be deleted
    lock.unlock();
    loop.run(ev::AUTO);

    return 0;
}

void manapi::net::http_pool::new_connection (ev::io &watcher, int revents) {
    worker->onrecv(worker);
}

manapi::net::site & manapi::net::http_pool::get_site() const {
    return *site;
}
