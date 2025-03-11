#include "async/ManapiAsyncSocket.hpp"

#include <fcntl.h>

std::shared_ptr<ev::io> manapi::async::pread_ready_mk_(std::shared_ptr<context> ctx, const int &fd,promise<void>::resolve_t resolve, promise<void>::reject_t reject) {
    auto w = ctx->eventloop()->create_watcher_fd(fd, ev::READ, [ctx, resolve = std::move(resolve), reject = std::move(reject)] (ev::io &w, int revents) mutable -> void {
        if ((revents & ev::READ)) {
            auto _resolve = std::move(resolve);
            auto ctx_ = ctx;
            ctx_->eventloop()->stop_watcher(w);
            ctx_->taskpool()->append_task([resolve = std::move(_resolve)] ()
                -> void { resolve(); });
        }
    });

    return std::move(w);
}

std::shared_ptr<ev::io> manapi::async::pwrite_ready_mk_(std::shared_ptr<context> ctx, const int &fd,promise<void>::resolve_t resolve, promise<void>::reject_t reject) {
    auto w = ctx->eventloop()->create_watcher_fd(fd, ev::WRITE, [ctx, resolve = std::move(resolve), reject = std::move(reject)] (ev::io &w, int revents) mutable -> void {
        if ((revents & ev::WRITE)) {
            auto _resolve = std::move(resolve);
            auto ctx_ = ctx;
            ctx_->eventloop()->stop_watcher(w);
            ctx_->taskpool()->append_task([resolve = std::move(_resolve)] ()
                -> void { resolve(); });
        }
    });

    return std::move(w);
}

manapi::future<void> manapi::async::read_ready(std::shared_ptr<context> ctx, int fd) {
    co_await promise<void> (ctx, [ctx, fd] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) -> future<> {
        auto w = pread_ready_mk_(ctx, fd, std::move(resolve), std::move(reject));
        co_await ctx->eventloop()->watch_fd(std::move(w));
    });
}

manapi::future<void> manapi::async::write_ready(std::shared_ptr<context> ctx, int fd) {
    co_await promise<void> (ctx, [ctx, fd] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) -> future<> {
        auto w = pwrite_ready_mk_(ctx, fd, std::move(resolve), std::move(reject));
        co_await ctx->eventloop()->watch_fd(std::move(w));
    });
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

manapi::future<void> manapi::async::read_ready(std::shared_ptr<context> ctx, int fd,cancellation_action cancellation) {
    co_await promise<void> (ctx, [ctx, fd, cancellation] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) mutable -> manapi::future<> {
        auto w = pread_ready_mk_(ctx, fd, resolve, std::move(reject));

        if (cancellation.contains_cancel_callback()) {
            cancellation.set_cancel_callback([ctx, w, resolve] () mutable -> manapi::future<> {
                co_await ctx->eventloop()->unwatch_fd(std::move(w));
                resolve ();
            });
        }

        co_await pio_ready(ctx, std::move(w), cancellation);

        cancellation.ready();
        co_return;
    });

    /** already */
    cancellation.disable_cancellation();
    cancellation.ready();
}

manapi::future<void> manapi::async::write_ready(std::shared_ptr<context> ctx, int fd,cancellation_action cancellation) {
    co_await promise<void> (ctx, [ctx, fd, cancellation] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) mutable -> manapi::future<> {
        auto w = pwrite_ready_mk_(ctx, fd, resolve, std::move(reject));

        if (cancellation.contains_cancel_callback()) {
            cancellation.set_cancel_callback([ctx, w, resolve] () mutable -> manapi::future<> {
                co_await ctx->eventloop()->unwatch_fd(std::move(w));
                resolve();
            });
        }

        co_await pio_ready(ctx, std::move(w), cancellation);

        cancellation.ready();
    });

    /** already */
    cancellation.disable_cancellation();
    cancellation.ready();
}