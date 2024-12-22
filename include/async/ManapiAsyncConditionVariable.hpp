#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "ManapiAsyncMutex.hpp"
#include "../ManapiAsync.hpp"

namespace manapi::net {
    class async_condition_variable {
    public:
        struct promise {
            std::function<bool()> cond;
            async_mutex *mx;
            std::shared_ptr<threadpool<task>> &taskpool;
            std::deque <std::pair<std::function<bool()>, std::coroutine_handle<future<>::promise>>> *stack;

            bool await_ready () noexcept { return false; }
            void await_resume () noexcept {}

            void await_suspend (std::coroutine_handle<future<>::promise> handle) {
                async::task_run(this->taskpool, this->mx->lock(), [cond = std::move(this->cond), stack = this->stack, mx = this->mx, handle = std::exchange(handle, nullptr)] () -> void {
                    stack->emplace_back(std::move(cond), handle);
                    mx->unlock();
                });
            }
        };

        async_condition_variable (std::shared_ptr<threadpool<task>> taskpool) : mx(taskpool), taskpool(taskpool) {}

        future<void> wait (const std::function<bool()> &cond) {
            if (cond()) { co_return; }
            co_await promise{cond, &this->mx, taskpool, &this->stack};
        }

        future<void> notify_one () {
            auto lk = co_await this->mx.lock_guard();
            _notify_first();
        }

        future<void> notify_all () {
            auto lk = co_await this->mx.lock_guard();
            int _len = this->stack.size();
            for (int i = 0; i < _len; i++) { this->_notify_first(); }
        }

        ~async_condition_variable () {
            this->stop.store(true);
            this->notify_all();
        }
    private:
        bool _notify_first () {
            if (this->stack.empty()) { return false; }
            auto data = this->stack.front();
            this->cnt.fetch_add(1);
            auto handle = [this, data = std::move(data)] () -> void {
                if (data.first ()) {
                    std:future<>::resume_promise(data.second);
                }
                else if (!this->stop.load()) {
                    this->stack.emplace_back(std::move(data.first), data.second);
                }

                this->cnt.fetch_sub(1);
            };
            this->stack.pop_front();
            if (this->stop.load()) {
                handle();
            }
            else {
                this->taskpool->append_task(std::move(handle));
            }
            return true;
        }
        std::atomic<bool> stop = false;
        std::atomic<int> cnt = 0;
        std::shared_ptr<threadpool<task>> taskpool;
        async_mutex mx;
        std::deque <std::pair<std::function<bool()>, std::coroutine_handle<future<>::promise>>> stack;
    };
}