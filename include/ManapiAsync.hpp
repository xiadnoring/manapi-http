#pragma once

#include <generator>
#include <memory>
#include <functional>
#include <iostream>

namespace manapi::net {
    struct promise_stack_data {
        std::function<void()> cb;
        bool finished;
        void *owner;
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

        std::suspend_always initial_suspend() { return {}; }

        void unhandled_exception () {
            exception = std::current_exception();
        }

        void run_connection_finish_data_ () {
            if(this->connection_finish_data != nullptr && this->connection_finish_data->finished) {
                this->connection_finish_data->cb();
            }
        }

        void _check_it_owner () {
            if (connection_finish_data != nullptr && connection_finish_data->owner == this) {
                this->connection_finish_data->finished = true;
            }
        }

        bool finished = false;
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
            std::coroutine_handle<> await_suspend (std::coroutine_handle<P> handle) noexcept {
                auto &promise = handle.promise();
                auto waiting = promise.waiting;

                if (waiting) {
                    promise.waiting = {};
                    return waiting;
                }

                return std::noop_coroutine();
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
        explicit future(std::coroutine_handle<promise> handle) : handle (handle){}
        ~future() {
            if (this->handle) {
                this->handle.destroy();
                this->handle = {};
            }
        }

        future (future &&n) noexcept {
            this->operator=(std::forward<decltype(n)>(n));
        }

        future &operator=(future &&n) noexcept {
            this->handle = n.handle;

            n.handle = {};
            return *this;
        }

        void operator()() {
            this->handle.resume();
        }

        bool await_ready () {
            return !handle || handle.done();
        }
        template <typename T1 = T>
        requires(std::is_same_v<T1, void>)
        void await_resume() {
            auto &promise = this->handle.promise();
            if (promise.exception) {
                std::rethrow_exception(promise.exception);
            }
        }

        template <typename T1 = T>
        requires(!std::is_same_v<T1, void>)
        T await_resume() {
            auto &promise = this->handle.promise();
            if (promise.exception) {
                std::rethrow_exception(promise.exception);
            }
            return std::move(promise.get_value());
        }

        template <typename T1>
        auto await_suspend (std::coroutine_handle<T1> handle) {
            auto &promise = this->handle.promise();
            promise.connection_finish_data = handle.promise().connection_finish_data;
            promise.waiting = handle;
            return this->handle;
        }

        // auto await_suspend (std::coroutine_handle<> handle) {
        //     auto &promise = this->handle.promise();
        //     promise.waiting = handle;
        //     return this->handle;
        // }
        void _on_connection_finish (const std::function<void()> &cb) {
            if (this->handle) {
                auto &promise = this->handle.promise();
                promise.connection_finish_data = std::make_shared<promise_stack_data>(
                    cb,
                    false,
                    &this->handle.promise()
                );
            }
        }

        [[nodiscard]] bool finished () const {
            return !this->handle || this->handle.promise().finished;
        }

        void destroy () {
            if (this->handle) {
                this->handle.destroy();
                this->handle = {};
            }
        }
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

        void await_suspend (std::coroutine_handle<future<>::promise> handle) {
            timerpool.append_timer(time, [handle] () -> void {
                handle.resume();
                auto &promise = handle.promise();
                promise.run_connection_finish_data_();
            });
        }
        void await_resume () const {}
    private:
        utils::timerpool &timerpool;
        std::chrono::seconds time{};
    };
}