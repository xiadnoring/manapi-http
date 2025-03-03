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

manapi::future<> manapi::async::pio_ready_(std::shared_ptr<context> ctx, std::shared_ptr<ev::io> w) {
    co_await ctx->eventloop()->watch_fd(std::move(w));
}

manapi::future<void> manapi::async::read_ready(std::shared_ptr<context> ctx, int fd) {
    co_await promise<void> (ctx, [ctx, fd] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) -> future<> {
        auto w = pread_ready_mk_(ctx, fd, std::move(resolve), std::move(reject));
        co_await pio_ready_(ctx, std::move(w));
    });
}

manapi::future<void> manapi::async::write_ready(std::shared_ptr<context> ctx, int fd) {
    co_await promise<void> (ctx, [ctx, fd] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) -> future<> {
        auto w = pwrite_ready_mk_(ctx, fd, std::move(resolve), std::move(reject));
        co_await pio_ready_(ctx, std::move(w));
    });
}

manapi::future<void> manapi::async::read_ready(std::shared_ptr<context> ctx, int fd,std::function<manapi::future<>()> *cancellation, std::function<void()> complete) {
    auto cb = std::make_shared<std::move_only_function<void()>> ([n = std::make_shared<std::atomic<bool>>(false), complete = std::move(complete)] () mutable -> void {
        if (!n->exchange(true)) {
            complete();
        }
    });

    co_await promise<void> (ctx, [cb, ctx, fd, cancellation] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) mutable -> manapi::future<> {
        auto w = pread_ready_mk_(ctx, fd, resolve, std::move(reject));

        if (cancellation) {
            (*cancellation) = [ctx, w, resolve] () mutable -> manapi::future<> {
                co_await ctx->eventloop()->unwatch_fd(std::move(w));
                resolve ();
            };
        }
        co_await pio_ready_(ctx, std::move(w));
        cb->operator()();
        co_return;
    });

    cb->operator()();
}

manapi::future<void> manapi::async::write_ready(std::shared_ptr<context> ctx, int fd,std::function<manapi::future<>()> *cancellation, std::function<void()> complete) {
    auto cb = std::make_shared<std::move_only_function<void()>> ([n = std::make_shared<std::atomic<bool>>(0), complete = std::move(complete)] () mutable -> void {
        if (!n->exchange(true)) {
            complete();
        }
    });

    co_await promise<void> (ctx, [cb, ctx, fd, cancellation] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) mutable -> manapi::future<> {
        auto w = pwrite_ready_mk_(ctx, fd, resolve, std::move(reject));

        if (cancellation) {
            (*cancellation) = [ctx, w, resolve] () mutable -> manapi::future<> {
                co_await ctx->eventloop()->unwatch_fd(std::move(w));
                resolve();
            };
        }

        co_await pio_ready_(ctx, std::move(w));
        cb->operator()();
    });
    cb->operator()();
}