#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "../ManapiAsync.hpp"

namespace manapi::net {
    class async_mutex {
    public:
        struct promise {
            std::mutex &mx;
            std::queue <std::coroutine_handle<future<>::promise> > &stack;
            bool &own;

            bool await_ready () noexcept { return false; }
            void await_resume () noexcept {}

            void await_suspend (std::coroutine_handle<future<>::promise> handle) {
                std::unique_lock <std::mutex> lk (this->mx);
                if (this->own) {
                    this->stack.push(std::exchange(handle, nullptr));
                }
                else {
                    this->own = true;
                    lk.unlock();
                    future<>::resume_promise(handle);
                }
            }
        };

        async_mutex (std::shared_ptr<threadpool<task>> taskpool) : taskpool(std::move(taskpool)), own(false) {}

        manapi::net::future<void> lock () {
            co_await async_mutex::promise {this->mx, this->stack, this->own};
            co_return;
        }

        bool try_to_lock () {
            std::lock_guard<std::mutex> lk (this->mx);
            if (this->own) { return false; }
            this->own = true;
            return true;
        }

        void unlock () {
            std::lock_guard<std::mutex> lk (this->mx);
            if (!this->own) { return; }
            if (this->stack.empty()) { own = false; return; }
            auto handle = this->stack.front();
            this->stack.pop();
            this->taskpool->append_task([handle = std::exchange(handle, nullptr)] () -> void {
                future<>::resume_promise(handle);
            });
        }

        future<utils::before_delete> lock_guard () {
            co_await this->lock();
            co_return utils::before_delete([this] () -> void {
                this->unlock();
            });
        }

        ~async_mutex () {
            std::lock_guard<std::mutex> lk (this->mx);
            if (!stack.empty()) {
                std::cerr << "~async_mutex(): i can't work anymore, i'm sorrryyy :(. stack.empty() != true\n";
            }
        }
    private:
        std::shared_ptr<threadpool<task>> taskpool;
        std::mutex mx;
        bool own;
        std::queue <std::coroutine_handle<future<>::promise> > stack;
    };
}