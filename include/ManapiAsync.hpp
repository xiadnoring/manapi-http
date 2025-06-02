#pragma once

#include <cassert>
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
        class cthread;
        extern size_t max_stack_depth;

        namespace internal {
            static thread_local std::size_t current_stack_cnt = 0;
            static thread_local std::shared_ptr<cthread> current_cthread_ = nullptr;
            const std::shared_ptr<threadpool<task>> &ethreadpool_(const std::shared_ptr<cthread> &ctx);
        }

        const std::shared_ptr<cthread> &current ();
    }

    class promise_base {
    public:
        promise_base() = default;

        ~promise_base() = default;

        void unhandled_exception () {
            this->exception = std::current_exception();
        }

        std::coroutine_handle<> waiting;
        std::exception_ptr exception;
    };

    template <typename T = void>
    class future
    {
        public:
        template<typename P>
        struct final_awaiter {
            bool await_ready () noexcept { return false; }

            template<typename T1 = T>
            requires(std::is_same_v<T, void>)
            auto await_suspend (std::coroutine_handle<P> handle) noexcept {
                auto &promise_ = handle.promise();
                auto waiting = std::exchange(promise_.waiting, nullptr);

                if (promise_.finish_cb) {
                    promise_.finish_cb->operator()(std::move(promise_.exception));
                }

                return waiting ? waiting : std::noop_coroutine();
            }

            template<typename T1 = T>
            requires(!std::is_same_v<T, void>)
            auto await_suspend (std::coroutine_handle<P> handle) noexcept {
                auto &promise_ = handle.promise();
                auto waiting = std::exchange(promise_.waiting, nullptr);

                if (promise_.finish_cb) {
                    if (promise_.exception) {
                        promise_.finish_cb->operator()(std::move(promise_.exception), nullptr);
                    }
                    else {
                        auto value_ = promise_.get_value();
                        promise_.finish_cb->operator()(std::move(promise_.exception), &value_);
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
                return {};
            }

            std::unique_ptr<std::move_only_function<void(std::exception_ptr err, T *v)>> finish_cb{nullptr};
            std::optional<T> value{};
        };

        using value_type = T;
        using promise_type = promise;
        explicit future(std::coroutine_handle<promise> handle) : handle_ (std::exchange(handle, nullptr)) {

        }

        ~future() {
            this->reset();
        }

        future (future &&n) noexcept {
            this->handle_ = std::exchange(n.handle_, nullptr);
        }

        future &operator=(future &&n) noexcept {
            this->handle_ = std::exchange(n.handle_, nullptr);

            return *this;
        }

        void reset () {
            if (this->handle_) {
                this->handle_.destroy();
                this->handle_ = nullptr;
            }
        }

        std::coroutine_handle<promise> release () {
            return std::exchange(this->handle_, nullptr);
        }

        void operator()() {
            this->resume_promise(this->handle_);
        }

        [[nodiscard]] bool operator==(const nullptr_t &n) const {
            return this->handle_ == nullptr;
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

                promise.waiting = handle;

                if (async::internal::current_stack_cnt >= async::max_stack_depth) {
                    auto &thr = manapi::async::current();
                    if (thr) {
                        async::internal::ethreadpool_(thr)->append_task([handle = this->handle] () -> void {
                             handle.resume();
                        });
                    }

                    return;
                }

                async::internal::current_stack_cnt++;
                this->handle.resume();
            }

            bool await_ready () {
                return !this->handle || this->handle.done();
            }
            template <typename T1 = T>
            requires(std::is_same_v<T, void>)
            void await_resume() {
                auto &promise_ = this->handle.promise();

                if (promise_.exception) {
                    std::rethrow_exception(promise_.exception);
                }
            }

            template <typename T1 = T>
            requires(!std::is_same_v<T, void>)
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

        template <typename T1 = T>
        requires(std::is_same_v<T, void>)
        void onfinish (std::move_only_function<void(std::exception_ptr err)> cb) {
            if (this->handle_) {
                auto &promise = this->handle_.promise();
                promise.finish_cb = std::move(std::make_unique<decltype(cb)>(std::move(cb)));
            }
        }

        template <typename T1 = T>
        requires(!std::is_same_v<T, void>)
        void onfinish (std::move_only_function<void(std::exception_ptr err, T *v)> cb) {
            if (this->handle_) {
                auto &promise = this->handle_.promise();
                promise.finish_cb = std::move(std::make_unique<decltype(cb)>(std::move(cb)));
            }
        }

        [[nodiscard]] bool finished () const {
            return !this->handle_ || this->handle_.done();
        }

        [[nodiscard]] const std::coroutine_handle<promise> &handle () {
            return this->handle_;
        }

        auto operator co_await () noexcept { return Awaiter{this->handle_}; }
    private:
        std::coroutine_handle<promise> handle_;
    };

    template<>
    class future<void>::promise : public promise_base {
    public:
        promise () : promise_base() {}
        promise (promise &&n) noexcept : promise_base(std::forward<decltype(n)>(n)) {}

        void return_void () const {}

        future<void>::final_awaiter<promise> final_suspend() noexcept {
            return {};
        }

        std::suspend_always initial_suspend() { return {}; }

        future<void> get_return_object()
        {
            return future<void>{ std::coroutine_handle<promise>::from_promise(*this) };
        }

        std::unique_ptr<std::move_only_function<void(std::exception_ptr err)>> finish_cb{nullptr};
    };
}