#include "worker/ManapiInterfaceWorker.hpp"

manapi::net::worker::interface_worker::interface_worker(net::http::site site, std::shared_ptr<worker::worker_config_t> data, manapi::net::http::config *config) : site_(std::move(site)), config_(config), worker_data_(std::move(data)) {
}

manapi::net::worker::interface_worker::~interface_worker() {
    if (this->global_.cleanup_global_cb) {
        this->global_.cleanup_global_cb(&this->global_, this);
    }
}

void manapi::net::worker::interface_worker::wrk_global(wrk_interface_global_t *data) {
    this->global_ = *data;
}

manapi::net::worker::wrk_interface_global_t * manapi::net::worker::interface_worker::wrk_global() {
    return &this->global_;
}


manapi::net::http::site & manapi::net::worker::interface_worker::site() {
    return this->site_;
}

manapi::net::http::config *manapi::net::worker::interface_worker::config() {
    return this->config_;
}

const std::shared_ptr<manapi::net::worker::worker_config_t> & manapi::net::worker::interface_worker::worker_data() {
    return this->worker_data_;
}

std::shared_ptr<manapi::net::worker::base> manapi::net::worker::interface_worker::copy() {
    return this->self_.lock();
}