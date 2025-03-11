#include "async/ManapiAsyncSocket.hpp"

#include <fcntl.h>

std::shared_ptr<ev::io> pio_ready_mk_(std::shared_ptr<manapi::async::context> ctx, int flags, const int &fd,manapi::async::promise<int>::resolve_t resolve, manapi::async::promise<int>::reject_t reject) {
    auto w = ctx->eventloop()->create_watcher_fd(fd, flags, [flags, ctx, resolve = std::move(resolve), reject = std::move(reject)] (ev::io &w, int revents) mutable -> void {
        if ((revents & flags)) {
            auto _resolve = std::move(resolve);
            auto ctx_ = ctx;
            ctx_->eventloop()->stop_watcher(w);
            ctx_->taskpool()->append_task([revents, resolve = std::move(_resolve)] ()
                -> void { resolve(revents); });
        }
    });

    return std::move(w);
}

manapi::future<> pio_ready (std::shared_ptr<manapi::async::context> ctx, std::shared_ptr<ev::io> w, manapi::async::cancellation_action cancellation) {
    return ctx->eventloop()->custom_callback([ctx, w, cancellation] (manapi::event_loop *ev) mutable -> void {
        if (cancellation.contains_timeout()) {
            cancellation.timeout_struct (ctx->timerpool()->append_timer_sync(cancellation.timeout(), [cancellation] (manapi::timer t) mutable
                -> void {
                cancellation.timeout_received();
            }));
        }

        /** bind watcher */
        w->start();
    });
}

manapi::future<int> manapi::async::custom_ready(std::shared_ptr<context> ctx, int flags, int fd) {
    co_return co_await promise<int> (ctx, [ctx, flags, fd] (promise<int>::resolve_t resolve, promise<int>::reject_t reject) -> future<> {
        auto w = pio_ready_mk_(ctx, flags, fd, std::move(resolve), std::move(reject));
        co_await ctx->eventloop()->watch_fd(std::move(w));
    });
}

manapi::future<int> manapi::async::read_ready(std::shared_ptr<context> ctx, int fd) {
    return custom_ready(std::move(ctx), ev::READ, fd);
}

manapi::future<int> manapi::async::write_ready(std::shared_ptr<context> ctx, int fd) {
    return custom_ready(std::move(ctx), ev::WRITE, fd);
}

manapi::future<int> manapi::async::custom_ready(std::shared_ptr<context> ctx, int flags, int fd, cancellation_action cancellation) {
    auto res = co_await promise<bool> (ctx, [ctx, flags, fd, cancellation] (promise<int>::resolve_t resolve, promise<int>::reject_t reject) mutable -> manapi::future<> {
            auto w = pio_ready_mk_(ctx, flags, fd, resolve, std::move(reject));

            if (cancellation.contains_cancel_callback()) {
                cancellation.set_cancel_callback([ctx, w, resolve] () mutable -> manapi::future<> {
                    co_await ctx->eventloop()->unwatch_fd(std::move(w));
                    resolve (-1);
                });
            }

            co_await pio_ready(ctx, std::move(w), cancellation);

            cancellation.ready();
            co_return;
        });

    /** already */
    cancellation.disable_cancellation();
    cancellation.ready();
    co_return res;
}

manapi::future<int> manapi::async::read_ready(std::shared_ptr<context> ctx, int fd,cancellation_action cancellation) {
    return custom_ready(std::move(ctx), ev::READ, fd, std::move(cancellation));
}

manapi::future<int> manapi::async::write_ready(std::shared_ptr<context> ctx, int fd,cancellation_action cancellation) {
    return custom_ready(std::move(ctx), ev::WRITE, fd, std::move(cancellation));
}