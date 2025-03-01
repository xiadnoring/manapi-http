#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    inline std::shared_ptr<ev::io> pread_ready_mk_ (const std::shared_ptr<context> &ctx, const int &fd, promise<void>::resolve_t resolve, promise<void>::reject_t reject) {
        auto w = ctx->eventloop()->create_watcher_fd(fd, ev::READ, [&ctx, resolve = std::move(resolve), reject = std::move(reject)] (ev::io &w, int revents) mutable -> void {
            if (revents & ev::READ) {
                auto _resolve = std::move(resolve);
                auto ctx_ = ctx;
                ctx_->eventloop()->stop_watcher(w);
                ctx_->taskpool()->append_task(std::move(_resolve));
            }
        });

        return std::move(w);
    }


    inline std::shared_ptr<ev::io> pwrite_ready_mk_ (const std::shared_ptr<context> &ctx, const int &fd, promise<void>::resolve_t resolve, promise<void>::reject_t reject) {
        auto w = ctx->eventloop()->create_watcher_fd(fd, ev::WRITE, [&ctx, resolve = std::move(resolve), reject = std::move(reject)] (ev::io &w, int revents) mutable -> void {
            if (revents & ev::WRITE) {
                auto _resolve = std::move(resolve);
                auto ctx_ = ctx;
                ctx_->eventloop()->stop_watcher(w);
                ctx_->taskpool()->append_task(std::move(_resolve));
            }
        });

        return std::move(w);
    }

    inline future<void> pio_ready_ (const std::shared_ptr<context> &ctx, std::shared_ptr<ev::io> w) {
        return ctx->eventloop()->watch_fd(std::move(w));
    }

    inline manapi::future<void> read_ready (const std::shared_ptr<context> &ctx, int fd) {

        co_await promise<void> (ctx, [&ctx, fd] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) -> future<> {
            auto w = pread_ready_mk_(ctx, fd, std::move(resolve), std::move(reject));
            co_await pio_ready_(ctx, std::move(w));
        });
    }

    inline manapi::future<void> write_ready (const std::shared_ptr<context> &ctx, int fd) {
        co_await promise<void> (ctx, [&ctx, fd] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) -> future<> {
            auto w = pwrite_ready_mk_(ctx, fd, std::move(resolve), std::move(reject));
            co_await pio_ready_(ctx, std::move(w));
        });
    }

    inline manapi::future<void> read_ready (const std::shared_ptr<context> &ctx, int fd, std::function<manapi::future<>()> *cancellation, std::function<void()> complete) {
        before_delete unlk ([complete = std::move(complete)] ()
            -> void { if (complete) { complete(); } });

        co_await promise<void, std::false_type> (ctx, [unlk = std::move(unlk), &ctx, fd, cancellation] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) mutable -> void {
            auto w = pread_ready_mk_(ctx, fd, resolve, std::move(reject));

            if (cancellation) {
                (*cancellation) = [ctx, w, resolve = std::move(resolve)] () mutable -> manapi::future<> {
                    co_await ctx->eventloop()->unwatch_fd(std::move(w));
                    resolve ();
                };
            }

            async::run (ctx, pio_ready_(ctx, std::move(w)), [unlk = std::move(unlk)] () mutable
                -> void {
                unlk.call();
            });
        });
    }

    inline manapi::future<void> write_ready (const std::shared_ptr<context> &ctx, int fd, std::function<manapi::future<>()> *cancellation, std::function<void()> complete) {
        before_delete unlk ([complete = std::move(complete)] ()
            -> void { if (complete) { complete(); } });

        co_await promise<void, std::false_type> (ctx, [unlk = std::move(unlk), &ctx, fd, cancellation] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) mutable -> void {
            auto w = pwrite_ready_mk_(ctx, fd, resolve, std::move(reject));

            if (cancellation) {
                (*cancellation) = [ctx, w, resolve = std::move(resolve)] () mutable -> manapi::future<> {
                    co_await ctx->eventloop()->unwatch_fd(std::move(w));
                    resolve();
                };
            }

            async::run (ctx, pio_ready_(ctx, std::move(w)), [unlk = std::move(unlk)] () mutable
                -> void {
                unlk.call();
            });
        });
    }

}