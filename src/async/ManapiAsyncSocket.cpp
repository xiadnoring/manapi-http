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

std::shared_ptr<ev::io> pio_ready_mk_(std::shared_ptr<manapi::async::context> ctx, int flags, const int &fd,manapi::async::promise<int>::resolve_t resolve, manapi::async::promise<int>::reject_t reject) {
    auto w = ctx->eventloop()->create_watcher_fd(fd, flags, [flags, ctx, resolve = std::move(resolve), reject = std::move(reject)] (ev::io &w, int revents) mutable -> void {
        if ((revents & flags)) {
            auto _resolve = std::move(resolve);
            auto ctx_ = ctx;
            ctx_->eventloop()->stop_watcher(w);
            _resolve(revents);
        }
    });

    return std::move(w);
}

manapi::future<> pio_ready (std::shared_ptr<manapi::async::context> ctx, std::shared_ptr<ev::io> w, manapi::async::promise<int>::resolve_t resolve, manapi::async::cancellation_action cancellation) {
    return ctx->eventloop()->custom_callback([ctx, resolve = std::move(resolve), w, cancellation] (manapi::event_loop *ev) mutable -> void {
        if (cancellation.contains_timeout()) {
            cancellation.timeout_struct ([resolve = std::move(resolve), ctx, w] () mutable
                -> void {
                ctx->eventloop()->stop_watcher(std::move(w));
                resolve(-1);
            });
        }

        /** bind watcher */
        w->start();
    });
}

#if MANAPIHTTP_NONUNIX
void manapi::async::set_non_blocking(fd_t fd) {

}
void manapi::async::close_descriptor(fd_t fd) {
    assert((false && "close_descriptor(...) without realization"))
}
#endif

void manapi::async::set_non_blocking(sd_t fd) {
#ifdef _WIN32
    u_long arg = 1;
    ioctlsocket(fd, FIONBIO, &arg);
#else
    int flgs = fcntl(fd, F_GETFL, 0);
    flgs |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flgs);
#endif
}

manapi::sd_t manapi::async::create_socket(int family, int protocol, int socktype, sockaddr *addr, socklen_t addrlen) {
    sd_t fd = socket(family, socktype, protocol);
    if (fd < 0) { THROW_MANAPIHTTP_EXCEPTION(ERR_SOCKET, "socket(...) returned an invalid value: {}", fd); }

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
        THROW_MANAPIHTTP_EXCEPTION(ERR_SOCKET, "setsockopt(...) failed: reuseaddr option. fd = {}", fd);
    }

    if (protocol == IPPROTO_TCP) {
        // if (0 > setsockopt(fd, protocol, TCP_NODELAY, &param_true, sizeof(param_true))) {
        //     THROW_MANAPIHTTP_EXCEPTION(ERR_SOCKET, "setsockopt(...) failed: tcp nodelay option. fd = {}", fd);
        // }
    }

    set_non_blocking(fd);

    bd.disable();

    return fd;
}

void manapi::async::close_descriptor(sd_t fd) {
#if MANAPIHTTP_NONUNIX
    ::closesocket(fd);
#else
    ::close(fd);
#endif
}

manapi::future<int> manapi::async::custom_ready(std::shared_ptr<context> ctx, int flags, fd_t fd) {
    co_return co_await promise<int> (ctx, [ctx, flags, fd] (promise<int>::resolve_t resolve, promise<int>::reject_t reject) -> future<> {
        auto w = pio_ready_mk_(ctx, flags, fd, std::move(resolve), std::move(reject));
        co_await ctx->eventloop()->watch_fd(std::move(w));
    });
}

manapi::future<int> manapi::async::read_ready(std::shared_ptr<context> ctx, fd_t fd) {
    return custom_ready(std::move(ctx), ev::READ, fd);
}

manapi::future<int> manapi::async::write_ready(std::shared_ptr<context> ctx, fd_t fd) {
    return custom_ready(std::move(ctx), ev::WRITE, fd);
}

manapi::future<int> manapi::async::custom_ready(std::shared_ptr<context> ctx, int flags, fd_t fd, cancellation_action cancellation) {
    auto res = co_await promise<int> (ctx, [ctx, flags, fd, cancellation] (promise<int>::resolve_t resolve, promise<int>::reject_t reject) mutable -> manapi::future<> {
            auto w = pio_ready_mk_(ctx, flags, fd, resolve, std::move(reject));

            if (cancellation.contains_cancel_callback()) {
                cancellation.set_cancel_callback([ctx, w, resolve] () mutable -> manapi::future<> {
                    co_await ctx->eventloop()->unwatch_fd(std::move(w));
                    resolve (-1);
                });
            }

            co_await pio_ready(ctx, std::move(w), std::move(resolve), cancellation);

            cancellation.ready();
            co_return;
        });

    /** already */
    cancellation.disable_cancellation();
    cancellation.ready();
    co_return res;
}

manapi::future<int> manapi::async::read_ready(std::shared_ptr<context> ctx, fd_t fd,cancellation_action cancellation) {
    return custom_ready(std::move(ctx), ev::READ, fd, std::move(cancellation));
}

manapi::future<int> manapi::async::write_ready(std::shared_ptr<context> ctx, fd_t fd,cancellation_action cancellation) {
    return custom_ready(std::move(ctx), ev::WRITE, fd, std::move(cancellation));
}