#pragma once

#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    inline manapi::future<void> read_ready (const std::shared_ptr<context> &ctx, const int &fd, std::function<manapi::future<>()> *cancellation = nullptr) {
        co_await promise<void> (ctx, [&ctx, fd, cancellation] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) -> future<> {
            auto w = co_await ctx->eventloop()->watch_fd(fd, ev::READ, [&ctx, resolve = std::move(resolve), reject = std::move(reject)] (ev::io &w, int revents) mutable -> void {
                if (revents & ev::READ) {
                    auto _resolve = std::move(resolve);
                    ctx->eventloop()->stop_watcher(w);
                    _resolve();
                }
            });

            if (cancellation) {
                (*cancellation) = [ctx, w] () mutable -> manapi::future<> {
                    return ctx->eventloop()->unwatch_fd(std::move(w));
                };
            }
        });
    }

    inline manapi::future<void> write_ready (const std::shared_ptr<context> &ctx, const int &fd, std::function<manapi::future<>()> *cancellation = nullptr) {
        co_await promise<void> (ctx, [&ctx, fd, cancellation] (promise<void>::resolve_t resolve, promise<void>::reject_t reject) -> future<> {
            auto w = co_await ctx->eventloop()->watch_fd(fd, ev::WRITE, [&ctx, resolve = std::move(resolve), reject = std::move(reject)] (ev::io &w, int revents) mutable -> void {
                if (revents & ev::WRITE) {
                    auto _resolve = std::move(resolve);
                    ctx->eventloop()->stop_watcher(w);
                    _resolve();
                }
            });

            if (cancellation) {
                (*cancellation) = [ctx, w] () mutable -> manapi::future<> {
                    return ctx->eventloop()->unwatch_fd(std::move(w));
                };
            }
        });
    }
}