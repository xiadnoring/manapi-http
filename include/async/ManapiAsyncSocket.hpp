#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiAsyncContext.hpp"
#include "ManapiCancellation.hpp"

namespace manapi::async {
    socket_t create_socket (int family, int protocol, int socktype, sockaddr *addr, socklen_t addrlen);
#if MANAPIHTTP_NONUNIX
    
#endif
    void close_descriptor (socket_t fd);
    void set_non_blocking (socket_t fd);

    manapi::future<int> custom_ready (socket_t flags, socket_t fd);
    manapi::future<int> read_ready (socket_t fd);
    manapi::future<int> write_ready (socket_t fd);
    manapi::future<int> custom_ready (int flags, socket_t fd, cancellation_action cancellation);
    manapi::future<int> read_ready (socket_t fd, cancellation_action cancellation);
    manapi::future<int> write_ready (socket_t fd, cancellation_action cancellation);
}
