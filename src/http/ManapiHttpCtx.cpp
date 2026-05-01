#include "http/ManapiHttpCtx.hpp"
#include "../include/ManapiUtils.hpp"

struct manapi::net::http::server_ctx::data_t {
    multithread_storage st;
};

manapi::net::http::server_ctx::server_ctx() {
    auto n = std::make_unique<worker_data_t>();
    this->m_data = std::make_unique <data_t>(
    multithread_storage (nullptr, static_cast<void*>(n.release()), +[] (void *ptr)
        -> void { delete static_cast<worker_data_t*> (ptr); }));
}

manapi::status_or<std::shared_ptr<manapi::net::http::server_ctx>> manapi::net::http::server_ctx::create() MANAPIHTTP_NOEXCEPT {
    try {
        return std::shared_ptr<manapi::net::http::server_ctx>(new manapi::net::http::server_ctx());
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return status_resource_exhausted();
    }
}

manapi::net::http::server_ctx::~server_ctx() = default;

manapi::multithread_storage & manapi::net::http::server_ctx::storage() {
    return this->m_data->st;
}
