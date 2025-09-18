#include "http/ManapiSiteCtx.hpp"
#include "../include/ManapiUtils.hpp"

struct manapi::net::http::server_ctx::data_t {
    multithread_storage st;
};

manapi::net::http::server_ctx::server_ctx() : data_(nullptr) {}

manapi::error::status_or<manapi::net::http::server_ctx> manapi::net::http::server_ctx::create() MANAPIHTTP_NOEXCEPT {
    try {
        server_ctx ctx;
        auto n = std::make_unique<worker_data_t>();
        ctx.data_ = std::make_shared<data_t>(
        multithread_storage (nullptr, static_cast<void*>(n.release()), +[] (void *ptr)
            -> void { delete static_cast<worker_data_t*> (ptr); }));
        return ctx;
    }
    catch (std::exception const &e) {
        manapi_log_error(e.what());
        return error::status_resource_exhausted();
    }
}

manapi::net::http::server_ctx::~server_ctx() = default;

manapi::net::http::server_ctx::server_ctx(const server_ctx &n) = default;

manapi::net::http::server_ctx & manapi::net::http::server_ctx::operator=(const server_ctx &n) = default;

manapi::net::http::server_ctx::server_ctx(server_ctx &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::net::http::server_ctx & manapi::net::http::server_ctx::operator=(server_ctx &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::multithread_storage & manapi::net::http::server_ctx::storage() {
    return this->data_->st;
}
