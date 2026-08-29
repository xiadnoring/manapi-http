#include <fcntl.h>

#include "ManapiEventLoop.hpp"
#include "std/ManapiSocket.hpp"
#include "../include/ManapiUtils.hpp"

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


static manapi::ev::io_cb manapi__pio_ready_mk_(int flags, manapi::socket_t fd,manapi::async::promise_sync<manapi::ev::status_or<int>>::resolve_t resolve, manapi::ctoken cancellation) {
    try {
        return [flags, resolve = std::move(resolve), cancellation = std::move(cancellation)]
            (const std::shared_ptr<manapi::ev::io> &w, int status, int revents) mutable
                -> void {
            auto resolve_ = std::move(resolve);
            cancellation.disable();

            manapi::async::current()->eventloop()->stop_watcher(w);

            if (!status && (revents & flags)) {
                resolve_(revents);
            }

            resolve_(manapi::ev::status_internal("io ready failed", status));
        };
    }
    catch (std::exception const &) {
        return nullptr;
    }
}

static void manapi__pio_ready (manapi::socket_t fd, int flags, manapi::ev::io_cb cb, const manapi::async::promise_sync<manapi::ev::status_or<int>>::resolve_t &resolve, manapi::ctoken cancellation) MANAPIHTTP_NOEXCEPT {
    if (!cb)
        goto err;

    try {
        auto wres = manapi::async::current()->eventloop()->create_watcher_socket(fd, std::move(cb));

        if (!wres)
            goto err;

        auto w = wres.unwrap();

        if (cancellation.contains_cancel_callback()) {
            cancellation.cancel_callback([w, resolve] () mutable
                -> void {
                    auto res_stop = w->stop();
                    assert(!res_stop);
                    manapi::async::current()->eventloop()->stop_watcher(w);
                    resolve (manapi::ev::status_cancelled("socket i/o operation has been cancelled"));
                });
        }

        /** bind watcher */
        if(w->start(flags)) {
            manapi::async::current()->eventloop()->stop_watcher(std::move(w));
            goto err;
        }
        return;
    }
    catch (std::exception const &e) {
        manapi_log_trace("%s failed due to %s", "io", e.what());
    }
    err: resolve (manapi::ev::status_internal("io:something gets wrong", manapi::ev::ERR_UNKNOWN));
}

#if MANAPIHTTP_NONUNIX

#endif

void manapi::async::set_non_blocking(socket_t fd) MANAPIHTTP_NOEXCEPT {
#ifdef _WIN32
    u_long arg = 1;
    ioctlsocket(fd, FIONBIO, &arg);
#else
    int flgs = fcntl(fd, F_GETFL, 0);
    flgs |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flgs);
#endif
}

manapi::status_or<manapi::socket_t> manapi::async::create_socket(int family, int protocol, int socktype) MANAPIHTTP_NOEXCEPT {
    socket_t fd = socket(family, socktype, protocol);
    if (fd < 0) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s fd=%d", "socket() failed", fd);
        return manapi::status_internal("socket() failed");
    }

#ifdef _WIN32
    char param_true = 1;
    char param_false = 0;
#else
    int param_true = 1;
    int param_false = 0;
#endif

    if (0 > setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &param_true, sizeof(param_true))) {
        manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "%s fd=%d", "setsockopt(): SO_REUSEADDR failed", fd);
        return manapi::status_internal("setsockopt(): SO_REUSEADDR failed");
    }

    if (protocol == IPPROTO_TCP) {
        // if (0 > setsockopt(fd, protocol, TCP_NODELAY, &param_true, sizeof(param_true)))
    }

    set_non_blocking(fd);

    return fd;
}

void manapi::async::close_descriptor(socket_t fd) MANAPIHTTP_NOEXCEPT {
#if MANAPIHTTP_NONUNIX
    ::closesocket(fd);
#else
    ::close(fd);
#endif
}

manapi::future<manapi::ev::status_or<int>> manapi::async::custom_ready(int flags, socket_t fd) {
    typedef manapi::async::promise_sync<manapi::ev::status_or<int>> promise;
    co_return co_await promise ([flags, fd] (promise::resolve_t resolve, promise::reject_t reject) -> void {
        auto cb = manapi__pio_ready_mk_(flags, fd, resolve, nullptr);
        auto wres = async::current()->eventloop()->create_watcher_socket(fd, std::move(cb));
        if (!wres) {
            resolve(wres.err());
            return;
        }
        auto s = wres.unwrap();
        s->start(flags);
    });
}

manapi::future<manapi::ev::status> manapi::async::read_ready(socket_t fd) {
    co_return (co_await custom_ready(ev::READ, fd)).err();
}

manapi::future<manapi::ev::status> manapi::async::write_ready(socket_t fd) {
    co_return (co_await custom_ready(ev::WRITE, fd)).err();
}

manapi::future<manapi::ev::status_or<int>> manapi::async::custom_ready(int flags, socket_t fd, ctoken cancellation) {
    using promise = promise_sync<manapi::ev::status_or<int>>;

    auto res = co_await  promise([flags, fd, cancellation] (promise::resolve_t resolve, promise::reject_t reject) mutable -> void {
        auto cb = manapi__pio_ready_mk_(flags, fd, resolve, cancellation);
        manapi__pio_ready(fd, flags, std::move(cb), resolve, std::move(cancellation));
    });

    /** already */
    cancellation.cancel();
    co_return std::move(res);
}

manapi::future<manapi::ev::status> manapi::async::read_ready(socket_t fd,ctoken cancellation) {
    co_return (co_await custom_ready(ev::READ, fd, std::move(cancellation))).err();
}

manapi::future<manapi::ev::status> manapi::async::write_ready(socket_t fd,ctoken cancellation) {
    co_return (co_await custom_ready(ev::WRITE, fd, std::move(cancellation))).err();
}

socklen_t manapi::async::socklen(const sockaddr *addr) MANAPIHTTP_NOEXCEPT {
    if (addr->sa_family == ev::IPv4) {
        return sizeof (sockaddr_in);
    }
    if (addr->sa_family == ev::IPv6) {
        return sizeof (sockaddr_in6);
    }

    return sizeof (sockaddr_storage);
}
