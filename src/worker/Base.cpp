#include <memory.h>

#include "worker/Base.hpp"

#include <fcntl.h>
#include <memory>

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

manapi::net::worker::base::base(net::site &site) :site(site) {}

manapi::net::worker::base::base(base &&n) noexcept : site(n.site) {
    this->config = std::move(n.config);
    this->read = std::move(n.read);
    this->write = std::move(n.write);
    this->loop = n.loop;

    n.loop = nullptr;
}

manapi::net::worker::base::~base() = default;

bool manapi::net::worker::base::is_valid_connection(worker::connection &connection) {
    return false;
}

void manapi::net::worker::base::set_config(std::shared_ptr<manapi::net::http::config> config) {
    this->config = std::move(config);
}

manapi::future<void> manapi::net::worker::base::connection_close(std::shared_ptr<connection> conn, bool clean_disconnect) { co_return; }

void manapi::net::worker::base::disable_watcher_for_status(connection &conn, const connection_status &status) {}

manapi::future<bool> manapi::net::worker::base::configure_connection(std::shared_ptr<connection> conn) { co_return false; }

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::base::accept(
    const std::function<std::shared_ptr<connection>()> &init) {
    return {};
}

std::optional<std::shared_ptr<manapi::net::worker::connection>> manapi::net::worker::base::accept() { return this->accept([] () -> std::shared_ptr<connection> { return {nullptr, [] (void *ptr) -> void { }}; }); }

void manapi::net::worker::base::onrecv(ev::io &watcher, int revents) {}

manapi::net::worker::base & manapi::net::worker::base::operator=(base &&n) noexcept {
    this->config = std::move(n.config);
    this->read = std::move(n.read);
    this->write = std::move(n.write);
    return *this;
}

void manapi::net::worker::base::set_fd_non_blocking(int fd) {
    int flgs = fcntl(fd, F_GETFL, 0);
    flgs |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flgs);
}

manapi::future<ssize_t> manapi::net::worker::base::response(worker::connection &connection, http_response &resp, bool finish) { co_return -1; }

std::shared_ptr<manapi::net::worker::base> manapi::net::worker::base::create(net::site &site, std::shared_ptr<manapi::net::http::config> config) {
    auto worker = std::make_shared<base>(site);
    worker->set_config(std::move(config));
    return std::move(worker);
}

void manapi::net::worker::base::_timeout(std::shared_ptr<connection> storage, const int &revents) {}


void manapi::net::worker::base::init() {}
