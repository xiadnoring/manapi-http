#pragma once

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../services/ManapiTimerPool.hpp"

namespace manapi::async {
    class delay {
    public:
        delay (const std::shared_ptr<context> &ctx, size_t ms) : ctx(ctx) {
            this->time = ms;
        }
        ~delay() = default;
        [[nodiscard]] bool await_ready () const {
            return false;
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            async::run(this->ctx->taskpool(), [timerpool_ = this->ctx->timerpool(), time = this->time, handle = std::exchange(handle, nullptr)] () -> future<void> {
                co_await timerpool_->async_append_timer_sync(time, [handle] (manapi::timer t) -> void {
                    future<>::resume_promise(handle);
                });
            });
        }
        void await_resume () const {}
    private:
        std::shared_ptr<context> ctx;
        size_t time{};
    };
}