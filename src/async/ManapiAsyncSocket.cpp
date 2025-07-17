#include "async/ManapiAsyncSocket.hpp"

#include <fcntl.h>
#if defined(__unix__)||defined(__APPLE__)
#   include <arpa/inet.h>
#   include <netinet/tcp.h>
#   include <netdb.h>
#   include <error.h>
#endif
#if defined(_WIN32)
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <winsock2.h>
#   include <ws2tcpip.h>
#endif

#include "../include/ManapiUtils.hpp"

manapi::ev::io_cb pio_ready_mk_(int flags, int fd,manapi::async::promise<int>::resolve_t resolve, manapi::async::promise<int>::reject_t reject, manapi::async::cancellation_action cancellation) {
    return [flags, resolve = std::move(resolve), reject = std::move(reject), cancellation = std::move(cancellation)]
        (const std::shared_ptr<manapi::ev::io> &w, int status, int revents) mutable
            -> void {
            if ((revents & flags)) {
                auto resolve_ = std::move(resolve);

                cancellation.disable();

                assert(!w->stop());
                manapi::async::current()->eventloop()->stop_watcher(w);

                resolve_(revents);
            }
        };
}

void pio_ready (manapi::socket_t fd, int flags, manapi::ev::io_cb cb, manapi::async::promise<int>::reject_t reject, manapi::async::cancellation_action cancellation) {
    auto w = manapi::async::current()->eventloop()->create_watcher_socket(fd, std::move(cb));

    if (cancellation.contains_cancel_callback()) {
        cancellation.cancel_callback([w, reject] () mutable
            -> void {
                assert(!w->stop());
                manapi::async::current()->eventloop()->stop_watcher(w);
                reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_CANCELLED, "socket i/o operation has been cancelled")));
            });
    }

    /** bind watcher */
    w->start(flags);
}

#if MANAPIHTTP_NONUNIX

#endif

void manapi::async::set_non_blocking(socket_t fd) {
#ifdef _WIN32
    u_long arg = 1;
    ioctlsocket(fd, FIONBIO, &arg);
#else
    int flgs = fcntl(fd, F_GETFL, 0);
    flgs |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flgs);
#endif
}

manapi::socket_t manapi::async::create_socket(int family, int protocol, int socktype, sockaddr *addr, socklen_t addrlen) {
    socket_t fd = socket(family, socktype, protocol);
    if (fd < 0) { THROW_MANAPIHTTP_EXCEPTION(ERR_FAILED_PRECONDITION, "socket(...) returned an invalid value: {}", fd); }

    before_delete bd ([fd] ()
        -> void { close_descriptor(fd); });

#ifdef _WIN32
    char param_true = 1;
    char param_false = 0;
#else
    int param_true = 1;
    int param_false = 0;
#endif

    if (0 > setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &param_true, sizeof(param_true))) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FAILED_PRECONDITION, "setsockopt(...) failed: reuseaddr option. fd = {}", fd);
    }

    if (protocol == IPPROTO_TCP) {
        // if (0 > setsockopt(fd, protocol, TCP_NODELAY, &param_true, sizeof(param_true))) {
        //     THROW_MANAPIHTTP_EXCEPTION(ERR_FAILED_PRECONDITION, "setsockopt(...) failed: tcp nodelay option. fd = {}", fd);
        // }
    }

    set_non_blocking(fd);

    bd.disable();

    return fd;
}

void manapi::async::close_descriptor(socket_t fd) {
#if MANAPIHTTP_NONUNIX
    ::closesocket(fd);
#else
    ::close(fd);
#endif
}

manapi::future<int> manapi::async::custom_ready(socket_t flags, socket_t fd) {
    co_return co_await promise<int, std::false_type> ([flags, fd] (promise<int>::resolve_t resolve, promise<int>::reject_t reject) -> void {
        auto cb = pio_ready_mk_(flags, fd, std::move(resolve), std::move(reject), nullptr);
        auto s = async::current()->eventloop()->create_watcher_socket(fd, std::move(cb));
        s->start(flags);
    });
}

manapi::future<int> manapi::async::read_ready(socket_t fd) {
    co_return co_await custom_ready(ev::READ, fd);
}

manapi::future<int> manapi::async::write_ready(socket_t fd) {
    co_return co_await custom_ready(ev::WRITE, fd);
}

manapi::future<int> manapi::async::custom_ready(int flags, socket_t fd, cancellation_action cancellation) {
    auto res = co_await promise<int> ([flags, fd, cancellation] (promise<int>::resolve_t resolve, promise<int>::reject_t reject) mutable -> manapi::future<> {
            auto cb = pio_ready_mk_(flags, fd, resolve, reject, cancellation);
            pio_ready(fd, flags, std::move(cb), std::move(reject), cancellation);
            co_return;
        });

    /** already */
    cancellation.cancel();
    co_return res;
}

manapi::future<int> manapi::async::read_ready(socket_t fd,cancellation_action cancellation) {
    return custom_ready(ev::READ, fd, std::move(cancellation));
}

manapi::future<int> manapi::async::write_ready(socket_t fd,cancellation_action cancellation) {
    return custom_ready(ev::WRITE, fd, std::move(cancellation));
}