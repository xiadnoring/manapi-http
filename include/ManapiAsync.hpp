#pragma once

#include <cassert>
#include <generator>
#include <memory>
#include <functional>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <utility>
#include <functional>

#include "ManapiUtils.hpp"
#include "services/ManapiThreadPool.hpp"
#include "services/ManapiTask.hpp"

namespace manapi {
    namespace async {
        extern size_t max_stack_depth;
    }

    class promise_base {
    public:
        promise_base() = default;

        ~promise_base() = default;

        promise_base(promise_base &&n) noexcept {
            this->operator=(std::forward<decltype(n)>(n));
        }

        promise_base &operator= (promise_base &&n) noexcept {
            this->finish_cb = std::move(n.finish_cb);
            this->exception = std::move(n.exception);
            this->waiting = std::exchange(n.waiting, {});

            return *this;
        }

        void unhandled_exception () {
            exception = std::current_exception();
        }

        int stack_deepth = 0;
        std::coroutine_handle<> waiting;
        std::exception_ptr exception;
        std::function<void()> finish_cb{nullptr};
        std::shared_ptr<threadpool<task>> taskpool{nullptr};
    };

    template <typename T = void>
    class future
    {
        public:
        template<typename P>
        struct final_awaiter {
            bool await_ready () noexcept { return false; }
            auto await_suspend (std::coroutine_handle<P> handle) noexcept {
                auto &_promise = handle.promise();
                auto waiting = std::exchange(_promise.waiting, nullptr);

                if (_promise.finish_cb) {
                    _promise.finish_cb();
                }

                return waiting ? waiting : std::noop_coroutine();
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
                this->value = std::move(t);
            }

            void return_value (const T &t) {
                this->value = t;
            }

            future get_return_object()
            {
                return future{ std::coroutine_handle<promise>::from_promise(*this) };
            }

            T get_value() {
                if (!this->value.has_value()) {
                    throw std::runtime_error("Pointer is null");
                }
                return std::move(this->value.value());
            }

            final_awaiter<promise> final_suspend() noexcept {
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
                auto mx = std::make_unique<std::mutex>();
                bool stop = false;
                {
                    mx->lock();
                    this->on_finish([&stop, &mx] () -> void {
                        stop = true;
                        mx->unlock();
                    }, std::move(taskpool));
                    this->resume_promise(this->handle);
                    mx->lock();
                }
            }
        }

        template <typename T1 = T>
        requires(!std::is_same_v<T1, void>)
        T1 get(std::shared_ptr<threadpool<task>> taskpool) {
            if (!this->handle.done()) {
                auto mx = std::make_unique<std::mutex>();
                bool stop = false;
                {
                    mx->lock();
                    this->on_finish([&stop, &mx] () -> void {
                        stop = true;
                        mx->unlock();
                    }, std::move(taskpool));
                    this->resume_promise(this->handle);
                    mx->lock();
                }
            }
            return std::move(this->handle.promise().get_value());
        }

        [[nodiscard]] bool operator==(const nullptr_t &n) const {
            return this->handle == nullptr;
        }

        [[nodiscard]] bool operator!=(const nullptr_t &n) const {
            return false == this->operator==(std::forward<decltype(n)>(n));
        }

        struct Awaiter {
            std::coroutine_handle<promise> handle;

            template <typename T1>
            void await_suspend (std::coroutine_handle<T1> handle) {
                auto &promise = this->handle.promise();
                auto &npromise = handle.promise();

                promise.stack_deepth = ++npromise.stack_deepth;
                promise.taskpool = npromise.taskpool;
                promise.waiting = handle;
                
                if (promise.taskpool && promise.stack_deepth >= async::max_stack_depth) {

                    promise.stack_deepth = 0;
                    promise.taskpool->append_task([handle = this->handle] () -> void {
                         handle.resume();
                    });

                    return;
                }

                this->handle.resume();
            }

            bool await_ready () {
                return !this->handle || this->handle.done();
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
            handle.resume();
        }

        void on_finish (const std::function<void()> &cb, std::shared_ptr<threadpool<task>> taskpool) {
            if (this->handle) {
                auto &promise = this->handle.promise();
                promise.finish_cb = cb;
                promise.taskpool = taskpool;
            }
        }

        [[nodiscard]] bool finished () const {
            return !this->handle || this->handle.done();
        }

        [[nodiscard]] const std::coroutine_handle<promise> &get_handle () {
            return this->handle;
        }

        auto operator co_await () noexcept { return Awaiter{this->handle}; }
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
            return {};
        }

        std::suspend_always initial_suspend() { return {}; }

        future get_return_object()
        {
            return future{ std::coroutine_handle<promise>::from_promise(*this) };
        }
    };
}

namespace manapi {
    namespace async {
        inline std::mutex async_tasks_mx;

        struct async_task_t {
            std::function <manapi::future<>()> cb;
            manapi::future<void> task;
        };

        inline std::unordered_map <size_t, std::shared_ptr<async_task_t>> async_tasks;

        inline size_t _run_prepare (std::shared_ptr<threadpool<task>> taskpool, manapi::future<> &task, std::function<void()> onfinish) {
            if (task.get_handle() == nullptr) {
                std::cerr << "Null pointer in the net::future<> task\n";
            }

            auto index = reinterpret_cast <size_t> (task.get_handle().address());

            task.on_finish([index, taskpool, onfinish = std::move(onfinish)] () -> void {
                if (onfinish) {
                    taskpool->append_task(onfinish);
                }

                decltype(async_tasks)::node_type data;
                {
                    std::lock_guard<std::mutex> lk (async_tasks_mx);
                    auto it = async_tasks.find(index);
                    if (it != async_tasks.end()) {
                        data = std::move(async_tasks.extract(it));
                        if (async_tasks.empty()) {
                            // free
                            async_tasks.clear();
                        }
                    }
                }
            }, taskpool);

            task();

            return index;
        }

        inline manapi::future<> invoke (auto && executer) {
            std::function<manapi::future<void>()> cb (std::forward<decltype(executer)>(executer));
            co_await cb();
        };

        inline void run(std::shared_ptr<threadpool<task>> taskpool, manapi::future<> task, const std::function<void()> &onfinish = nullptr) {
            const size_t index = _run_prepare(std::move(taskpool), task, onfinish);

            if (!task.finished()) {
                std::lock_guard<std::mutex> lk (async_tasks_mx);
                if (async_tasks.contains(index)) {
                    printf("bug\n");
                }
                async_tasks.insert({index, std::make_shared<async_task_t>(nullptr, std::move(task))});
            }
        }

        inline void run (std::shared_ptr<threadpool<task>> taskpool, auto && executor,  const std::function<void()> &onfinish = nullptr) {
            async::run (std::move(taskpool), invoke(std::forward<decltype(executor)>(executor)), onfinish);
        }
    }
}