#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "ManapiAsyncMutex.hpp"
#include "../ManapiAsync.hpp"
#include "components/ManapiChain.hpp"

namespace manapi::async {
    class condition_variable {
    private:
        struct notify_sub_t {
            std::coroutine_handle<future<>::promise> handle;
            std::function<bool()> cond;
            async::mutex *mx;
        };
    public:
        struct promise {
            std::function<bool()> cond;
            async::mutex *gmx;
            async::mutex *mx;
            std::shared_ptr<threadpool<task>> &taskpool;
            chain <notify_sub_t> *stack;

            bool await_ready () noexcept { return false; }
            void await_resume () noexcept {}

            void await_suspend (std::coroutine_handle<future<>::promise> handle) {
                async::run(this->taskpool, this->gmx->lock(), [cond = std::move(this->cond), mx = this->mx, stack = this->stack, gmx = this->gmx, handle = std::exchange(handle, nullptr)] () -> void {
                    stack->push({handle, std::move(cond), mx});
                    gmx->unlock();
                });
            }
        };

        condition_variable (const std::shared_ptr<threadpool<task>> &taskpool) : mx(taskpool), taskpool(taskpool) {}

        future<void> wait (const std::function<bool()> &cond) {
            if (cond()) { co_return; }
            co_await promise{cond, &this->mx, nullptr, taskpool, &this->stack};
        }

        future<void> wait (async::mutex &mx, const std::function<bool()> &cond) {
            if (cond()) {
                if (!mx.locked()) {
                    co_await mx.lock();
                }
                co_return;
            }
            mx.unlock();
            co_await promise{cond, &this->mx, &mx, taskpool, &this->stack};
        }

        future<void> notify_one () {
            auto lk = co_await this->mx.lock_guard();
            co_await this->_notify_first();
        }

        future<void> notify_all () {
            auto lk = co_await this->mx.lock_guard();
            size_t _len = this->stack.size();
            for (size_t i = 0; i < _len; i++) {
                if (!co_await this->_notify_first()) {
                    break;
                }
            }
        }

        ~condition_variable () = default;
    private:
        future<void> _notify_item (chain<notify_sub_t>::iterator it) {
            auto &data = *it;
            // if (data.mx) {
            //     //MANAPIHTTP_LOG2("--WANT 2 BLOCK");
            //     co_await data.mx->lock();
            //     //MANAPIHTTP_LOG2("--BLOCKED");
            // }

            bool rhs = false;
            try { rhs = data.cond (); }
            catch (std::exception const &e) { std::cerr << e.what() << "\n"; }

            if (rhs) {
                auto handle = std::exchange(data.handle, nullptr);
                {
                    auto lk = co_await this->mx.lock_guard();
                    this->stack.erase(it);
                }
                //MANAPIHTTP_LOG2("--pop_front");
                this->taskpool->append_task([handle = std::exchange(handle, nullptr)] () -> void {
                    future<>::resume_promise(handle);
                });
            }
            else {
                //MANAPIHTTP_LOG2("--UNBLOCKED");
                if (data.mx) { data.mx->unlock(); }

                if (this->stop) {
                    auto lk = co_await this->mx.lock_guard();
                    this->stack.erase(it);
                }
            }
        }
        future<bool> _notify_first () {
            if (this->stack.empty()) { co_return false; }
            auto &data = *this->stack.rbegin();
            if (data.mx) { co_await data.mx->lock(); }
            async::run(this->taskpool, this->_notify_item(this->stack.rbegin()));
            co_return true;
        }
        std::atomic<bool> stop = false;
        std::atomic<int> cnt = 0;
        std::shared_ptr<threadpool<task>> taskpool;
        async::mutex mx;
        chain <notify_sub_t> stack;
    };
}
