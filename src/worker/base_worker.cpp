#include "worker/base_worker.hpp"
#include <memory.h>
#ifdef _WIN32
#   include <processthreadsapi.h>
#endif
#include <fcntl.h>
#include <memory>

manapi::net::worker::connection::connection(void *ptr, void(*eraser)(void*)): ptr (ptr, eraser) {}

manapi::net::worker::connection::connection(connection &&n)  noexcept : ptr (std::move(n.ptr)) {
    this->client = n.client;
    this->len = n.len;

    memset(&n.client, 0, sizeof (n.client)); n.len = 0;
}

manapi::net::worker::base::base(net::site &site) :site(site) {}

manapi::net::worker::base::~base() = default;

void manapi::net::worker::base::set_config(std::shared_ptr<manapi::net::http::config> config) {
    this->config = std::move(config);
}

manapi::future<ssize_t> manapi::net::worker::base::fwrite(connection &conn,const void *buff, ssize_t size, bool finish) {
    ssize_t total = 0;
    ssize_t rhs = 0;
    while (total < size) {
        rhs = co_await this->write(conn, static_cast<const char *>(buff) + total, size - total, finish);
        if (rhs <= 0) {
            co_return rhs;
        }
        total += rhs;
    }
    co_return total;
}

manapi::future<ssize_t> manapi::net::worker::base::fread(connection &conn, void *buff, ssize_t size) {
    ssize_t total = 0;
    ssize_t rhs = 0;
    while (total < size) {
        rhs = co_await this->read(conn, static_cast<char *>(buff) + total, size - total);
        if (rhs < 0) {
            co_return rhs;
        }
        if (rhs == 0) {
            co_return total;
        }
        total += rhs;
    }
    co_return total;
}

manapi::future<long int> manapi::net::worker::base::response(worker::connection &connection, http::response &resp,
    bool finish) {
    co_return -1;
}
