#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiAsyncContext.hpp"
#include "ManapiCancellation.hpp"

namespace manapi::async {
    sd_t create_socket (int family, int protocol, int socktype, sockaddr *addr, socklen_t addrlen);
#if MANAPIHTTP_NONUNIX
    void set_non_blocking (fd_t fd);
    void close_descriptor (fd_t fd);
#endif
    void close_descriptor (sd_t fd);
    void set_non_blocking (sd_t fd);

    manapi::future<int> custom_ready (std::shared_ptr<context> ctx, int flags, fd_t fd);
    manapi::future<int> read_ready (std::shared_ptr<context> ctx, fd_t fd);
    manapi::future<int> write_ready (std::shared_ptr<context> ctx, fd_t fd);
    manapi::future<int> custom_ready (std::shared_ptr<context> ctx, int flags, fd_t fd, cancellation_action cancellation);
    manapi::future<int> read_ready (std::shared_ptr<context> ctx, fd_t fd, cancellation_action cancellation);
    manapi::future<int> write_ready (std::shared_ptr<context> ctx, fd_t fd, cancellation_action cancellation);
}
