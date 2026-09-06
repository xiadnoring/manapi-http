#include "http/ManapiHttpCtx.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/ManapiHttpInternal.hpp"

manapi::net::http::server_ctx::server_ctx() {
    this->m_data = std::make_unique <server_ctx::data_t>();
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

manapi::net::http::server_ctx::data_t & manapi::net::http::server_ctx::storage() {
    return *this->m_data;
}
