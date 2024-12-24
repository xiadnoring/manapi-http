#pragma once

#include <generator>
#include <memory>
#include <functional>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <utility>
#include <utility>

#include "services/ManapiThreadPool.hpp"
#include "services/ManapiTask.hpp"

namespace manapi::net {
    constexpr int max_stack_depth = 500;

    struct promise_stack_data {
        std::function<void()> cb;
        std::atomic<bool> finished;
        std::atomic<bool> already;
        void *owner;
        std::shared_ptr<threadpool<task>> taskpool;
    };


    class promise_base {
    public:
        promise_base() = default;

        ~promise_base() = default;

        promise_base(promise_base &&n) noexcept {
            this->operator=(std::forward<decltype(n)>(n));
        }

        promise_base &operator= (promise_base &&n) noexcept {
            this->connection_finish_data = std::move(n.connection_finish_data);
            this->exception = std::move(n.exception);
            this->finished = n.finished;
            this->waiting = n.waiting;

            n.waiting = {};
            n.finished = false;
            return *this;
        }

        void unhandled_exception () {
            exception = std::current_exception();
        }

        void _check_it_owner () {
            if (connection_finish_data != nullptr && connection_finish_data->owner == this) {
                this->connection_finish_data->finished.store(true);
            }
        }

        bool finished = false;
        int stack_deepth = 0;
        std::coroutine_handle<> waiting;
        std::exception_ptr exception;
        std::shared_ptr<promise_stack_data> connection_finish_data;
    };

    template <typename T = void>
    class future
    {
        public:
        template<typename P>
        struct final_awaiter {
            bool await_ready () noexcept { return false; }
            auto await_suspend (std::coroutine_handle<P> handle) noexcept {
                std::shared_ptr<promise_stack_data> &data = handle.promise().connection_finish_data;
                auto waiting = handle.promise().waiting;
                if (data && (&handle.promise()) == data->owner && data->finished && !data->already) {
                    if (waiting != nullptr) {
                        printf("no\n");
                    }
                    else {
                        auto tmp = data;
                        tmp->already.store(true);
                        tmp->cb();
                    }
                }
                return  waiting ? waiting : std::noop_coroutine();
            }
            void await_resume () noexcept {}
        };

        class promise : public promise_base
        {
        public:
            promise () : promise_base() {}

            promise (promise &&n) noexcept : promise_base(std::forward<decltype(n)>(n)) {}

            std::suspend_always yield_value(T value) {
                this->value = std::move(value);
                return {};
            }

            std::suspend_always initial_suspend() { return {}; }

            void return_value (T &&t) {
                value = std::move(t);
            }

            void return_value (const T &t) {
                value = t;
            }

            future get_return_object()
            {
                return future{ std::coroutine_handle<promise>::from_promise(*this) };
            }

            T get_value() {
                if (!value.has_value()) {
                    throw std::runtime_error("Pointer is null");
                }
                return std::move(value.value());
            }

            final_awaiter<promise> final_suspend() noexcept {
                this->finished = true;
                this->_check_it_owner();
                return {};
            }
        private:
            std::optional<T> value{};
        };

        using value_type = T;
        using promise_type = promise;
        explicit future(std::coroutine_handle<promise> handle) : handle (std::exchange(handle, nullptr)) {

        }

        ~future() {
            if (this->handle) {
                this->handle.destroy();
            }
        }

        future (future &&n) noexcept {
            this->operator=(std::forward<decltype(n)>(n));
        }

        future &operator=(future &&n) noexcept {
            this->handle = std::exchange(n.handle, nullptr);

            return *this;
        }

        operator future<void> () noexcept {
            return future<void> (this->handle);
        }

        void operator()() {
            this->resume_promise(this->handle);
        }

        template <typename T1 = T>
        requires(std::is_same_v<T1, void>)
        void get(std::shared_ptr<threadpool<task>> taskpool) {
            if (!this->handle.done()) {
                std::mutex mx;
                mx.lock();
                this->_on_connection_finish([&mx] () -> void {
                    mx.unlock();
                }, std::move(taskpool));
                this->resume_promise(this->handle);
                mx.lock();
            }
        }

        struct Awaiter {
            std::coroutine_handle<promise> handle;

            template <typename T1>
            void await_suspend (std::coroutine_handle<T1> handle) {
                auto &promise = this->handle.promise();
                promise.stack_deepth = ++handle.promise().stack_deepth;
                promise.connection_finish_data = handle.promise().connection_finish_data;
                promise.waiting = handle;
                if (promise.connection_finish_data != nullptr && promise.stack_deepth > max_stack_depth) {
                    promise.stack_deepth = 0;
                    promise.connection_finish_data->taskpool->append_task([handle = this->handle] () -> void {
                         handle.resume();
                    });
                    return;
                }

                this->handle.resume();
            }

            bool await_ready () {
                return !handle || handle.done();
            }
            template <typename T1 = T>
            requires(std::is_same_v<T1, void>)
            void await_resume() {
                auto &promise_ = this->handle.promise();

                if (promise_.exception) {
                    std::rethrow_exception(promise_.exception);
                }
            }

            template <typename T1 = T>
            requires(!std::is_same_v<T1, void>)
            T await_resume() {
                auto &promise_ = this->handle.promise();
                if (promise_.exception) {
                    std::rethrow_exception(promise_.exception);
                }

                return std::move(promise_.get_value());
            }
        };

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        static void resume_promise (const std::coroutine_handle<T1> &handle) {
            auto cnd = handle.promise().connection_finish_data;
            handle.resume();
            if (cnd != nullptr && cnd->finished.load()) {
                if (!cnd->already) {
                    cnd->already.store(true);
                    cnd->cb();
                }
            }
            cnd.reset();
        }

        // auto await_suspend (std::coroutine_handle<> handle) {
        //     auto &promise = this->handle.promise();
        //     promise.waiting = handle;
        //     return this->handle;
        // }
        void _on_connection_finish (const std::function<void()> &cb, std::shared_ptr<threadpool<task>> taskpool) {
            if (this->handle) {
                auto &promise = this->handle.promise();
                promise.connection_finish_data = std::make_shared<promise_stack_data>(
                    cb,
                    false,
                    false,
                    &this->handle.promise(),
                    taskpool
                );
            }
        }

        [[nodiscard]] bool finished () const {
            return !this->handle || this->handle.promise().finished;
        }

        [[nodiscard]] const std::coroutine_handle<promise> &get_handle () {
            return handle;
        }

        auto operator co_await () noexcept { return Awaiter{handle}; }
    private:
        std::coroutine_handle<promise> handle;
    };

    template<>
    class future<void>::promise : public promise_base {
    public:
        promise () : promise_base() {}
        promise (promise &&n) noexcept : promise_base(std::forward<decltype(n)>(n)) {}

        void return_void () const {}

        final_awaiter<promise> final_suspend() noexcept {
            this->finished = true;
            this->_check_it_owner();
            return {};
        }

        std::suspend_always initial_suspend() { return {}; }

        future get_return_object()
        {
            return future{ std::coroutine_handle<promise>::from_promise(*this) };
        }
    };
}

#include "services/ManapiTimerPool.hpp"
namespace manapi::net {
    class async_delay {
    public:
        async_delay (utils::timerpool &timerpool, const std::chrono::seconds &time) : timerpool(timerpool) {
            this->time = time;
        }
        ~async_delay() = default;
        [[nodiscard]] bool await_ready () const {
            return false;
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            timerpool.append_timer(time, [handle] () -> void {
                future<>::resume_promise(handle);
            });
        }
        void await_resume () const {}
    private:
        utils::timerpool &timerpool;
        std::chrono::seconds time{};
    };

    namespace async {
        inline std::atomic <ssize_t> async_cnt = 0;
        inline std::mutex async_tasks_mx;
        inline std::unordered_map <size_t, manapi::net::future<void>> async_tasks;

        inline void task_run(std::shared_ptr<threadpool<task>> taskpool, manapi::net::future<> task, const std::function<void()> &onfinish = nullptr) {
            if (task.get_handle() == nullptr) {
                std::cerr << "Null pointer in the net::future<> task\n";
            }

            auto index = reinterpret_cast <size_t> (task.get_handle().address());

            task._on_connection_finish([index, taskpool, onfinish] () -> void {
                if (onfinish) taskpool->append_task(onfinish);
                decltype(async_tasks)::node_type data;
                {
                    // can be self-locked
                    std::lock_guard<std::mutex> lk (async_tasks_mx);
                    auto it = async_tasks.find(index);
                    if (it != async_tasks.end()) {
                        data = std::move(async_tasks.extract(it));
                        async_cnt.fetch_sub(1);
                    }
                }
            }, taskpool);

            task();

            if (!task.finished()) {
                std::lock_guard<std::mutex> lk (async_tasks_mx);
                async_tasks.insert({index, std::move(task)});
                async_cnt.fetch_add(1);
            }
        }
    }
}

namespace manapi::net {
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