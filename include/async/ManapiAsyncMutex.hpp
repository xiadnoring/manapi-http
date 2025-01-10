#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "../ManapiAsync.hpp"
#include "../ManapiBeforeDelete.hpp"

namespace manapi::async {
    class mutex {
    public:
        struct promise {
            std::mutex &mx;
            std::queue <std::coroutine_handle<future<>::promise> > &stack;
            std::optional<std::thread::id> &own;

            bool await_ready () noexcept { return false; }
            void await_resume () noexcept {}

            void await_suspend (std::coroutine_handle<future<>::promise> handle) {
                std::unique_lock <std::mutex> lk (this->mx);
                if (this->own.has_value()) {
                    this->stack.push(std::exchange(handle, nullptr));
                }
                else {
                    this->own = std::this_thread::get_id();
                    lk.unlock();
                    future<>::resume_promise(handle);
                }
            }
        };

        mutex (std::shared_ptr<threadpool<task>> taskpool) : taskpool(std::move(taskpool)) {}

        manapi::future<void> lock () {
            co_await async::mutex::promise {this->mx, this->stack, this->own};
            co_return;
        }

        bool try_to_lock () {
            std::lock_guard<std::mutex> lk (this->mx);
            if (this->own.has_value()) { return false; }
            this->own = std::this_thread::get_id();
            return true;
        }

        void unlock () {
            std::lock_guard<std::mutex> lk (this->mx);
            if (!this->own.has_value()) {
                return;
            }
            if (this->stack.empty()) {
                this->own.reset();
                return;
            }
            auto handle = this->stack.front();
            this->stack.pop();
            if (this->stack.empty()) {
                stack = {}; // free
            }
            this->taskpool->append_task([handle = std::exchange(handle, nullptr)] () -> void {
                future<>::resume_promise(handle);
            });
        }

        [[nodiscard]] bool locked () {
            std::lock_guard<std::mutex> lk (this->mx);
            return this->own.has_value();
        }

        future<before_delete> lock_guard () {
            co_await this->lock();
            co_return before_delete([this] () -> void {
                this->unlock();
            });
        }

        ~mutex () {
            std::lock_guard<std::mutex> lk (this->mx);
            if (!stack.empty()) {
                std::cerr << "~async::mutex(): i can't work anymore, i'm sorrryyy :(. stack.empty() != true\n";
            }
        }
    private:
        std::shared_ptr<threadpool<task>> taskpool;
        std::mutex mx;
        std::optional<std::thread::id> own;
        std::queue <std::coroutine_handle<future<>::promise> > stack;
    };
}