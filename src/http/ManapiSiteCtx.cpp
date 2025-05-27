#include "http/ManapiSiteCtx.hpp"

manapi::net::http::server_ctx::server_ctx() {
    this->data_ = std::make_shared<data_t>();

    this->data_->servers=(std::make_shared<worker::server_config_t>(
            std::make_unique<async::tmutex>(), std::make_unique<async::tmutex>()));
}

manapi::net::http::server_ctx::~server_ctx() = default;

manapi::net::http::server_ctx::server_ctx(server_ctx &&n) noexcept = default;

manapi::net::http::server_ctx & manapi::net::http::server_ctx::operator=(server_ctx &&n) noexcept = default;

manapi::net::http::server_ctx::server_ctx(const server_ctx &n) = default;

manapi::net::http::server_ctx & manapi::net::http::server_ctx::operator=(const server_ctx &n) = default;

std::shared_ptr<manapi::net::worker::worker_config_t> manapi::net::http::server_ctx::worker_config(std::size_t id) {
    std::lock_guard<std::mutex> lk (this->data_->mx);

    if (this->data_->workers.size() == id) {
        this->data_->workers.push_back(std::make_shared<worker::worker_config_t>(
            std::make_unique<async::tmutex>()));
    }
    else if (this->data_->workers.size() < id) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_BUG, "provided id doesn't exists or can't be init");
    }

    return this->data_->workers[id];
}

std::shared_ptr<manapi::net::worker::server_config_t> manapi::net::http::server_ctx::server_config(ev::shared_async w) {
    std::lock_guard<std::mutex> lk (this->data_->mx);

    this->data_->server_subs.push_back(std::move(w));

    return this->data_->servers;
}

void manapi::net::http::server_ctx::next_time(std::atomic<size_t> *n) {
    n->fetch_add(1);
}

void manapi::net::http::server_ctx::server_notify_subs() {
    std::lock_guard<std::mutex> lk (this->data_->mx);

    for (const auto &sub : this->data_->server_subs) {
        sub->send();
    }
}

void manapi::net::http::server_ctx::remove_server_sub(ev::shared_async w) {
    std::lock_guard<std::mutex> lk (this->data_->mx);

    auto it = std::find(this->data_->server_subs.begin(), this->data_->server_subs.end(), w);
    if (it != this->data_->server_subs.end()) {
        this->data_->server_subs.erase(it);
    }
}

void manapi::net::http::server_ctx::remove_workers() {
    std::lock_guard<std::mutex> lk (this->data_->mx);

    this->data_->workers = {};
}



