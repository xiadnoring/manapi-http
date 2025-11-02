#pragma once

#include <cassert>
#include <memory>
#include <functional>
#include <mutex>
#include <utility>
#include <functional>
#include <coroutine>
#include <optional>

#include "./std/ManapiFunction.hpp"
#include "./ManapiUtils.hpp"

namespace manapi {
    class threadpool;
}

namespace manapi::async {
    class mutex_locker;

    class cthread;

    const std::shared_ptr<cthread> &current () MANAPIHTTP_NOEXCEPT;

    bool context_exists () MANAPIHTTP_NOEXCEPT;
}

namespace manapi::async::internal {
    const std::shared_ptr<threadpool> &ethreadpool_(const std::shared_ptr<cthread> &ctx) MANAPIHTTP_NOEXCEPT;

    const std::shared_ptr<cthread> &current_ () MANAPIHTTP_NOEXCEPT;

    void current_ (std::shared_ptr<cthread> ctx) MANAPIHTTP_NOEXCEPT;

    std::size_t current_stack_cnt_crt () MANAPIHTTP_NOEXCEPT;

    void current_stack_cnt_set (std::size_t cnt) MANAPIHTTP_NOEXCEPT;

    std::size_t max_stack_depth_crt () MANAPIHTTP_NOEXCEPT;

    void max_stack_depth_set (std::size_t cnt) MANAPIHTTP_NOEXCEPT;

    bool future_final_awaiter_ready () MANAPIHTTP_NOEXCEPT;

    void append_static_task (manapi::fixed_function<void()> callback) MANAPIHTTP_NOEXCEPT;

    class promise_base_future {
    public:
        promise_base_future();

        ~promise_base_future();

        void unhandled_exception ();

        std::coroutine_handle<> waiting;

        std::exception_ptr exception;
    };

    template<typename T, typename P>
    struct final_awaiter {
        bool await_ready () MANAPIHTTP_NOEXCEPT {
            return async::internal::future_final_awaiter_ready();
        }

        template<typename T1 = T>
        requires(std::is_same_v<T, void>)
        std::coroutine_handle<> await_suspend (std::coroutine_handle<P> handle) MANAPIHTTP_NOEXCEPT;

        template<typename T1 = T>
        requires(!std::is_same_v<T, void>)
        auto await_suspend (std::coroutine_handle<P> handle) MANAPIHTTP_NOEXCEPT {
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

        void await_resume () MANAPIHTTP_NOEXCEPT {}
    };

    template<typename T, typename F>
    class promise : public promise_base_future
    {
    public:
        promise () = default;

        promise (promise &&n) MANAPIHTTP_NOEXCEPT = default;

        std::suspend_always yield_value(T value) {
            this->value = std::move(value);
            return {};
        }

        std::suspend_always initial_suspend() { return {}; }

        void return_value (const T &t) {
            this->value = t;
        }

        void return_value (T &&t) {
            this->value = std::move(t);
        }

        F get_return_object()
        {
            return F{ std::coroutine_handle<promise>::from_promise(*this) };
        }

        T get_value() {
            if (!this->value.has_value()) {
                throw std::runtime_error("Pointer is null");
            }
            return std::move(this->value.value());
        }

        final_awaiter<T, promise<T, F>> final_suspend() MANAPIHTTP_NOEXCEPT { return {}; }

        std::unique_ptr<std::move_only_function<void(std::exception_ptr err, T *v)>> finish_cb{nullptr};
        std::optional<T> value{};
    };

    void future_final_awaiter_suspend (std::coroutine_handle<promise_base_future> original, std::coroutine_handle<promise_base_future> handle) MANAPIHTTP_NOEXCEPT;
}

namespace manapi {
    template <typename T = void>
    class future
    {
    public:
        using value_type = T;
        using promise_type = async::internal::promise<T, manapi::future<T>>;

        explicit future(std::coroutine_handle<promise_type> handle) : handle_ (std::exchange(handle, nullptr)) {}

        ~future() { this->reset(); }

        future (future &&n) MANAPIHTTP_NOEXCEPT {
            this->handle_ = std::exchange(n.handle_, nullptr);
        }

        future &operator=(future &&n) MANAPIHTTP_NOEXCEPT {
            if (this != &n) {
                this->handle_ = std::exchange(n.handle_, nullptr);
            }
            return *this;
        }

        void reset () {
            if (this->handle_) {
                this->handle_.destroy();
                this->handle_ = nullptr;
            }
        }

        std::coroutine_handle<promise_type> release () MANAPIHTTP_NOEXCEPT {
            return std::exchange(this->handle_, nullptr);
        }

        void operator()() MANAPIHTTP_NOEXCEPT {
            this->handle_.resume();
        }

        MANAPIHTTP_NODISCARD bool operator==(const std::nullptr_t &n) const {
            return this->handle_ == nullptr;
        }

        MANAPIHTTP_NODISCARD bool operator!=(const std::nullptr_t &n) const {
            return false == this->operator==(std::forward<decltype(n)>(n));
        }

        struct Awaiter {
            std::coroutine_handle<promise_type> handle;

            template <typename T1>
            void await_suspend (std::coroutine_handle<T1> handle) {
                //async::internal::future_final_awaiter_suspend(this->handle, handle);
                auto &promise = this->handle.promise();
                //auto &npromise = handle.promise();

                promise.waiting = handle;

                auto current_stack_cnt_ = manapi::async::internal::current_stack_cnt_crt ();
                if (current_stack_cnt_ >= async::internal::max_stack_depth_crt()) {
                    auto &thr = manapi::async::internal::current_();
                    if (thr) {
                        async::internal::append_static_task([original = this->handle] () -> void {
                             original.resume();
                        });
                    }

                    return;
                }

                manapi::async::internal::current_stack_cnt_set (current_stack_cnt_ + 1);
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

        template <typename T1 = T>
        requires(std::is_same_v<T, void>)
        void onfinish (std::move_only_function<void(std::exception_ptr err)> cb) {
            this->onfinish(std::make_unique<decltype(cb)>(std::move(cb)));
        }

        template <typename T1 = T>
        requires(!std::is_same_v<T, void>)
        void onfinish (std::move_only_function<void(std::exception_ptr err, T *v)> cb) {
            this->onfinish(std::make_unique<decltype(cb)>(std::move(cb)));
        }

        template <typename T1 = T>
        requires(std::is_same_v<T, void>)
        void onfinish (std::unique_ptr<std::move_only_function<void(std::exception_ptr err)>> cb) MANAPIHTTP_NOEXCEPT {
            if (this->handle_) {
                auto &promise = this->handle_.promise();
                promise.finish_cb = std::move(cb);
            }
        }

        template <typename T1 = T>
        requires(!std::is_same_v<T, void>)
        void onfinish (std::unique_ptr<std::move_only_function<void(std::exception_ptr err, T *v)>> cb) MANAPIHTTP_NOEXCEPT {
            if (this->handle_) {
                auto &promise = this->handle_.promise();
                promise.finish_cb = std::move(cb);
            }
        }

        MANAPIHTTP_NODISCARD bool finished () const {
            return !this->handle_ || this->handle_.done();
        }

        MANAPIHTTP_NODISCARD const std::coroutine_handle<promise_type> &handle () {
            return this->handle_;
        }

        Awaiter operator co_await () MANAPIHTTP_NOEXCEPT {
            return Awaiter{this->handle_};
        }
    private:
        std::coroutine_handle<promise_type> handle_;
    };
}

namespace manapi::async::internal {
    template<>
    class promise<void, manapi::future<>> : public promise_base_future {
    public:
        promise ();

        promise (promise &&n) MANAPIHTTP_NOEXCEPT;

        void return_void ();

        final_awaiter<void, promise<void, manapi::future<>>> final_suspend() MANAPIHTTP_NOEXCEPT;

        std::suspend_always initial_suspend();

        future<void> get_return_object();

        std::unique_ptr<std::move_only_function<void(std::exception_ptr err)>> finish_cb{nullptr};
    };

    std::coroutine_handle<> future_final_awaiter_suspend (std::coroutine_handle<promise<void, manapi::future<>>> handle) MANAPIHTTP_NOEXCEPT;
}

template<typename T, typename P>
template<typename T1> requires (std::is_same_v<T, void>)
std::coroutine_handle<> manapi::async::internal::final_awaiter<T, P>::await_suspend(std::coroutine_handle<P> handle) MANAPIHTTP_NOEXCEPT {
    return future_final_awaiter_suspend(handle);
}
