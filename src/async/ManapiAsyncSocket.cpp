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

manapi::ev::io_cb pio_ready_mk_(std::shared_ptr<manapi::async::context> ctx, int flags, const int &fd,manapi::async::promise<int>::resolve_t resolve, manapi::async::promise<int>::reject_t reject, manapi::async::cancellation_action cancellation) {
    return [flags, ctx, resolve = std::move(resolve), reject = std::move(reject), cancellation = std::move(cancellation)]
        (std::shared_ptr<manapi::ev::io> &w, int status, int revents) mutable
            -> void {
            if ((revents & flags)) {
                auto resolve_ = std::move(resolve);
                auto ctx_ = std::move(ctx);

                cancellation.disable_cancellation();

                assert(!w->stop());
                ctx_->eventloop()->stop_watcher(w);

                resolve_(revents);
            }
        };
}

manapi::future<> pio_ready (std::shared_ptr<manapi::async::context> ctx, manapi::fd_t fd, int flags, manapi::ev::io_cb cb, manapi::async::promise<int>::reject_t reject, manapi::async::cancellation_action cancellation) {
    return ctx->eventloop()->custom_callback([ctx, fd, flags, reject = std::move(reject), cb = std::move(cb), cancellation] (manapi::event_loop *ev) mutable
        -> void {
        auto w = ctx->eventloop()->create_watcher_fd(fd, std::move(cb));

        if (cancellation.contains_cancel_callback()) {
            cancellation.cancel_callback([w, reject] (std::shared_ptr<manapi::async::context> ctx) mutable
                -> void {
                    assert(!w->stop());
                    ctx->eventloop()->stop_watcher(w);
                    reject (std::make_exception_ptr(RETHROW_MANAPIHTTP_EXCEPTION2(manapi::ERR_CANCELLED, "socket i/o operation has been cancelled")));
                });
        }

        /** bind watcher */
        w->start(flags);
    });
}

#if MANAPIHTTP_NONUNIX
void manapi::async::set_non_blocking(fd_t fd) {

}
void manapi::async::close_descriptor(fd_t fd) {
    assert((false && "close_descriptor(...) without realization"))
}
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

void manapi::async::close_descriptor(socket_t fd) {
#if MANAPIHTTP_NONUNIX
    ::closesocket(fd);
#else
    ::close(fd);
#endif
}

manapi::future<int> manapi::async::custom_ready(std::shared_ptr<context> ctx, fd_t flags, fd_t fd) {
    co_return co_await promise<int> (ctx, [ctx, flags, fd] (promise<int>::resolve_t resolve, promise<int>::reject_t reject) -> future<> {
        auto cb = pio_ready_mk_(ctx, flags, fd, std::move(resolve), std::move(reject), nullptr);
        co_await ctx->eventloop()->watch_poll(fd, flags, std::move(cb));
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
            auto cb = pio_ready_mk_(ctx, flags, fd, resolve, reject, cancellation);
            co_await pio_ready(ctx, fd, flags, std::move(cb), std::move(reject), cancellation);
            co_return;
        });

    /** already */
    cancellation.cancel();
    co_return res;
}

manapi::future<int> manapi::async::read_ready(std::shared_ptr<context> ctx, fd_t fd,cancellation_action cancellation) {
    return custom_ready(std::move(ctx), ev::READ, fd, std::move(cancellation));
}

manapi::future<int> manapi::async::write_ready(std::shared_ptr<context> ctx, fd_t fd,cancellation_action cancellation) {
    return custom_ready(std::move(ctx), ev::WRITE, fd, std::move(cancellation));
}