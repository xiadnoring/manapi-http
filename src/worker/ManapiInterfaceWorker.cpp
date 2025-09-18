#include "worker/ManapiInterfaceWorker.hpp"
#include "../include/ManapiUtils.hpp"

manapi::net::worker::interface_worker::interface_worker(net::http::site site, std::shared_ptr<multithread_storage::worker_t> data, manapi::net::http::config *config)
    : site_(std::move(site)), config_(config), worker_data_(std::move(data)), worker_pool_id_(0), global_() {
    this->deep_worker_id_ = 0;
    this->flags_ = 0;
}

manapi::net::worker::interface_worker::~interface_worker() {
    if (this->global_.cleanup_global_cb) {
        this->global_.cleanup_global_cb(&this->global_, this);
    }
}

void manapi::net::worker::interface_worker::wrk_global(wrk_interface_global_t *data) MANAPIHTTP_NOEXCEPT {
    this->global_ = *data;
}

manapi::net::worker::wrk_interface_global_t * manapi::net::worker::interface_worker::wrk_global() MANAPIHTTP_NOEXCEPT {
    return &this->global_;
}


manapi::net::http::site & manapi::net::worker::interface_worker::site() MANAPIHTTP_NOEXCEPT {
    return this->site_;
}

manapi::net::http::config *manapi::net::worker::interface_worker::config() MANAPIHTTP_NOEXCEPT {
    return this->config_;
}

int manapi::net::worker::interface_worker::worker_flags() MANAPIHTTP_NOEXCEPT {
    return this->flags_;
}


void manapi::net::worker::interface_worker::worker_pool_id(std::size_t worker_pool_id) MANAPIHTTP_NOEXCEPT {
    this->worker_pool_id_ = worker_pool_id;
}

std::size_t manapi::net::worker::interface_worker::worker_pool_id() const MANAPIHTTP_NOEXCEPT {
    return this->worker_pool_id_;
}

std::size_t manapi::net::worker::interface_worker::deep_worker_id() const MANAPIHTTP_NOEXCEPT {
    return this->deep_worker_id_;
}

const std::shared_ptr<manapi::multithread_storage::worker_t> & manapi::net::worker::interface_worker::worker_data() MANAPIHTTP_NOEXCEPT {
    return this->worker_data_;
}