#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    std::shared_ptr<ev::io> pread_ready_mk_ (std::shared_ptr<context> ctx, const int &fd, promise<void>::resolve_t resolve, promise<void>::reject_t reject);
    std::shared_ptr<ev::io> pwrite_ready_mk_ (std::shared_ptr<context> ctx, const int &fd, promise<void>::resolve_t resolve, promise<void>::reject_t reject);
    future<void> pio_ready_ (std::shared_ptr<context> ctx, std::shared_ptr<ev::io> w);
    manapi::future<void> read_ready (std::shared_ptr<context> ctx, int fd);
    manapi::future<void> write_ready (std::shared_ptr<context> ctx, int fd);
    manapi::future<void> read_ready (std::shared_ptr<context> ctx, int fd, std::function<manapi::future<>()> *cancellation, std::function<void()> complete);
    manapi::future<void> write_ready (std::shared_ptr<context> ctx, int fd, std::function<manapi::future<>()> *cancellation, std::function<void()> complete);

}