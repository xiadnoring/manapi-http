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
    struct promise_stack_data {
        std::function<void()> cb;
        bool finished;
        bool already;
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
            this->finish_data = std::move(n.finish_data);
            this->exception = std::move(n.exception);
            this->finished = std::exchange(n.finished, false);
            this->waiting = std::exchange(n.waiting, {});

            return *this;
        }

        void unhandled_exception () {
            exception = std::current_exception();
        }

        void _check_it_owner () {
            if (finish_data != nullptr && finish_data->owner == this) {
                this->finish_data->finished = true;
            }
        }

        bool finished = false;
        int stack_deepth = 0;
        std::coroutine_handle<> waiting;
        std::exception_ptr exception;
        std::shared_ptr<promise_stack_data> finish_data{nullptr};
        std::vector<std::shared_ptr<std::function<void()>>> resume_data{};
    };

    template <typename T = void>
    class future
    {
        public:
        template<typename P>
        struct final_awaiter {
            bool await_ready () noexcept { return false; }
            auto await_suspend (std::coroutine_handle<P> handle) noexcept {
                std::shared_ptr<promise_stack_data> &data = handle.promise().finish_data;
                auto waiting = std::exchange(handle.promise().waiting, nullptr);
                if (data && (&handle.promise()) == data->owner && data->finished && !data->already) {
                    if (waiting != nullptr) {
                        printf("no\n");
                    }
                    else {
                        auto tmp = data;
                        tmp->already = true;
                        tmp->cb();
                    }
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
                promise.stack_deepth = npromise.stack_deepth + 1;
                promise.finish_data = npromise.finish_data;
                promise.waiting = handle;
                promise.resume_data.insert(promise.resume_data.end(), npromise.resume_data.begin(), npromise.resume_data.end());
                if (promise.finish_data != nullptr && promise.stack_deepth >= async::max_stack_depth) {
                    promise.stack_deepth = 0;
                    promise.finish_data->taskpool->append_task([handle = this->handle] () -> void {
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
            auto &_promise = handle.promise();
            auto cnd = _promise.finish_data;
            auto &rsm = _promise.resume_data;

            for (const auto &cb: rsm) {
                cb->operator()();
            }

            handle.resume();

            if (cnd && cnd->finished && !cnd->already) {
                cnd->already = true;
                cnd->cb();
            }
        }

        // auto await_suspend (std::coroutine_handle<> handle) {
        //     auto &promise = this->handle.promise();
        //     promise.waiting = handle;
        //     return this->handle;
        // }
        void on_finish (const std::function<void()> &cb, std::shared_ptr<threadpool<task>> taskpool) {
            if (this->handle) {
                auto &promise = this->handle.promise();
                promise.finish_data = std::shared_ptr<promise_stack_data> (new promise_stack_data {
                    cb,
                    false,
                    false,
                    &this->handle.promise(),
                    taskpool
                }, [] (promise_stack_data *ptr) -> void {
                    delete ptr;
                });
            }
        }

        void on_resume (const std::function<void()> &cb) {
            if (this->handle) {
                promise &_promise = this->handle.promise();
                _promise.resume_data.push_back(
                    std::make_shared<std::function<void()>>(cb));
            }
        }

        [[nodiscard]] bool finished () const {
            return !this->handle || this->handle.promise().finished;
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