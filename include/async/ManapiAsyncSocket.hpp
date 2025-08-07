#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiAsyncContext.hpp"
#include "ManapiCancellation.hpp"

namespace manapi::async {
    manapi::error::status_or<socket_t> create_socket (int family, int protocol, int socktype) MANAPIHTTP_NOEXCEPT;
#if MANAPIHTTP_NONUNIX
    
#endif
    void close_descriptor (socket_t fd) MANAPIHTTP_NOEXCEPT;
    void set_non_blocking (socket_t fd) MANAPIHTTP_NOEXCEPT;

    manapi::future<manapi::sys_error::status_or<int>> custom_ready (int flags, socket_t fd);
    manapi::future<manapi::sys_error::status> read_ready (socket_t fd);
    manapi::future<manapi::sys_error::status> write_ready (socket_t fd);
    manapi::future<manapi::sys_error::status_or<int>> custom_ready (int flags, socket_t fd, cancellation_action cancellation);
    manapi::future<manapi::sys_error::status> read_ready (socket_t fd, cancellation_action cancellation);
    manapi::future<manapi::sys_error::status> write_ready (socket_t fd, cancellation_action cancellation);
}
