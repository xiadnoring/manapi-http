#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    inline future<std::shared_ptr<ev::io>> pread_ready_ (const std::shared_ptr<context> &ctx, int fd, promise<void>::resolve_t resolve, promise<void>::reject_t reject) {
        return ctx->eventloop()->watch_fd(fd, ev::READ, [&ctx, resolve = std::move(resolve), reject = std::move(reject)] (ev::io &w, int revents) mutable -> void {
            if (revents & ev::READ) {
                auto _resolve = std::move(resolve);
                auto ctx_ = ctx;
                ctx_->eventloop()->stop_watcher(w);
                ctx_->taskpool()->append_task(std::move(_resolve));
            }
        });
    }

    inline future<std::shared_ptr<ev::io>> pwrite_ready_ (const std::shared_ptr<context> &ctx, int fd, promise<void>::resolve_t resolve, promise<void>::reject_t reject) {
        return ctx->eventloop()->watch_fd(fd, ev::WRITE, [&ctx, resolve = std::move(resolve), reject = std::move(reject)] (ev::io &w, int revents) mutable -> void {
            if (revents & ev::WRITE) {
                auto _resolve = std::move(resolve);
                auto ctx_ = ctx;
                ctx_->eventloop()->stop_watcher(w);
                ctx_->taskpool()->append_task(std::move(_resolve));
            }
        });
    }

    inline manapi::future<void> read_ready (const std::shared_ptr<context> &ctx, int fd) {
        co_await promise<void> (ctx, [&ctx, fd] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) -> future<> {
            auto w = co_await pread_ready_(ctx, fd, std::move(resolve), std::move(reject));
        });
    }

    inline manapi::future<void> write_ready (const std::shared_ptr<context> &ctx, int fd) {
        co_await promise<void> (ctx, [&ctx, fd] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) -> future<> {
            auto w = co_await pwrite_ready_(ctx, fd, std::move(resolve), std::move(reject));
        });
    }

    inline manapi::future<void> read_ready (const std::shared_ptr<context> &ctx, int fd, std::function<manapi::future<>()> *cancellation, std::function<void()> complete) {
        before_delete unlk ([complete = std::move(complete)] ()
            -> void { if (complete) { complete(); } });

        co_await promise<void> (ctx, [unlk = std::move(unlk), &ctx, fd, cancellation] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) mutable -> future<> {
            auto w = co_await pread_ready_(ctx, fd, resolve, std::move(reject));

            if (cancellation) {
                (*cancellation) = [ctx, w, resolve = std::move(resolve)] () mutable -> manapi::future<> {
                    co_await ctx->eventloop()->unwatch_fd(std::move(w));
                    resolve ();
                };
            }

            unlk.call();
        });
    }

    inline manapi::future<void> write_ready (const std::shared_ptr<context> &ctx, int fd, std::function<manapi::future<>()> *cancellation, std::function<void()> complete) {
        before_delete unlk ([complete = std::move(complete)] ()
            -> void { if (complete) { complete(); } });

        co_await promise<void> (ctx, [unlk = std::move(unlk), &ctx, fd, cancellation] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) mutable -> future<> {
            auto w = co_await pwrite_ready_(ctx, fd, resolve, std::move(reject));

            if (cancellation) {
                (*cancellation) = [ctx, w, resolve = std::move(resolve)] () mutable -> manapi::future<> {
                    co_await ctx->eventloop()->unwatch_fd(std::move(w));
                    resolve();
                };
            }

            unlk.call();
        });
    }

}