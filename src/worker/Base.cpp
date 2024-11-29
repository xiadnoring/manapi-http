#include "worker/Base.hpp"

manapi::net::worker::connection::connection(void *ptr, void(*eraser)(void*)): ptr (ptr, eraser) {}

manapi::net::worker::connection::connection(connection &&n)  noexcept : ptr (std::move(n.ptr)) {
    this->client = n.client;
    this->len = n.len;

    memset(&n.client, 0, sizeof (n.client)); n.len = 0;
}

manapi::net::worker::connection & manapi::net::worker::connection::operator=(connection &&n) noexcept {
    this->ptr = std::move(n.ptr);
    this->client = n.client;
    this->len = n.len;

    memset(&n.client, 0, sizeof (n.client)); n.len = 0;
    return *this;
}

manapi::net::worker::base::base() {}

manapi::net::worker::base::base(base &&n) noexcept {
    this->config = std::move(n.config);
    this->read = std::move(n.read);
    this->write = std::move(n.write);
}

manapi::net::worker::base::~base() = default;

bool manapi::net::worker::base::is_valid_connection(worker::connection &connection) {
    return false;
}

void manapi::net::worker::base::set_config(std::shared_ptr<manapi::net::http::config> config) {
    this->config = std::move(config);
}

bool manapi::net::worker::base::configure_connection(connection &conn) const { return false; }

manapi::net::worker::connection manapi::net::worker::base::accept() {  }

manapi::net::worker::base & manapi::net::worker::base::operator=(base &&n) noexcept {
    this->config = std::move(n.config);
    this->read = std::move(n.read);
    this->write = std::move(n.write);
    return *this;
}

ssize_t manapi::net::worker::base::response(worker::connection &connection, http_response &resp, bool finish) { return -1; }

void manapi::net::worker::base::init() {}
