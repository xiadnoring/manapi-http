#pragma once

#include "../ManapiAsync.hpp"
#include "services/ManapiTimerPool.hpp"

namespace manapi {
    class async_delay {
    public:
        async_delay (timerpool &timerpool, const std::chrono::seconds &time) : _timerpool(timerpool) {
            this->time = time;
        }
        ~async_delay() = default;
        [[nodiscard]] bool await_ready () const {
            return false;
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            async::run(this->_timerpool.get_task_pool(), [&timerpool = this->_timerpool, time = this->time, handle = std::exchange(handle, nullptr)] () -> future<void> {
                co_await timerpool.async_append_timer_sync(time, [handle] () -> void {
                    future<>::resume_promise(handle);
                });
            });
        }
        void await_resume () const {}
    private:
        timerpool &_timerpool;
        std::chrono::seconds time{};
    };

    class async_thread {
    public:
        async_thread (const std::function<void()> &cb) {
            this->cb = cb;
        }
        ~async_thread() = default;
        [[nodiscard]] bool await_ready () const {
            return false;
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            std::jthread t ([cb = std::move(this->cb), handle] () -> void {
                cb();
                future<>::resume_promise(handle);
            });
            t.detach();
        }
        void await_resume () const {}
    private:
        std::function<void()> cb;
    };
}