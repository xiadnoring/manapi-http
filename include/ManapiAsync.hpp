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
    class ethreadpool;

    class mthreadpool;

    class event_loop;

    class timerpool;

    class logger;

    class object_pool;

    template<typename T>
    class future;
}

namespace manapi::async {
    class mutex_locker;

    class cthread;

    const std::shared_ptr<cthread> &current () MANAPIHTTP_NOEXCEPT;

    const std::shared_ptr<event_loop> &eventloop () MANAPIHTTP_NOEXCEPT;

    const std::shared_ptr<timerpool> &etimerpool () MANAPIHTTP_NOEXCEPT;

    const std::shared_ptr<ethreadpool> &etaskpool () MANAPIHTTP_NOEXCEPT;

    const std::shared_ptr<mthreadpool> &mtaskpool () MANAPIHTTP_NOEXCEPT;

    object_pool* memory_fabric () MANAPIHTTP_NOEXCEPT;

    const std::shared_ptr<logger> &log () MANAPIHTTP_NOEXCEPT;

    bool context_exists () MANAPIHTTP_NOEXCEPT;

    void append_task (const std::coroutine_handle<> &handle);

    void append_task (std::move_only_function<void()>&& callback);
}

namespace manapi::async::internal {
    class promise_base_future;

    template<typename T>
    class promise;

    const std::shared_ptr<cthread> &current_ () MANAPIHTTP_NOEXCEPT;

    void current_ (std::shared_ptr<cthread> ctx) MANAPIHTTP_NOEXCEPT;

    std::size_t current_stack_cnt_crt () MANAPIHTTP_NOEXCEPT;

    void current_stack_cnt_set (std::size_t cnt) MANAPIHTTP_NOEXCEPT;

    std::size_t max_stack_depth_crt () MANAPIHTTP_NOEXCEPT;

    bool future_final_awaiter_ready () MANAPIHTTP_NOEXCEPT;

    void future_awaiter_suspend(promise_base_future *promise, const std::coroutine_handle<> &handle, const std::coroutine_handle<> &waiting);

    class promise_base_future {
    public:
        promise_base_future();

        virtual ~promise_base_future();

        void unhandled_exception ();

        std::coroutine_handle<> final_awaiter_suspend () MANAPIHTTP_NOEXCEPT;

        std::suspend_always initial_suspend();

        virtual void run_finish_cb () MANAPIHTTP_NOEXCEPT = 0;

        std::coroutine_handle<> waiting;

        std::exception_ptr m_exception;
    };

    template<typename T>
    struct final_awaiter {
        bool await_ready () MANAPIHTTP_NOEXCEPT {
            return async::internal::future_final_awaiter_ready();
        }

        std::coroutine_handle<> await_suspend (std::coroutine_handle<promise<T>> handle) MANAPIHTTP_NOEXCEPT {
            return static_cast<promise_base_future *> (&handle.promise())->final_awaiter_suspend();
        }

        void await_resume () MANAPIHTTP_NOEXCEPT {}
    };

    template<typename T>
    class promise : public promise_base_future
    {
    public:
        promise () = default;

        promise (promise &&n) MANAPIHTTP_NOEXCEPT = default;

        ~promise() override = default;

        std::suspend_always yield_value(T value) {
            this->m_value = std::move(value);
            return {};
        }

        void return_value (const T &t) {
            this->m_value = t;
        }

        void return_value (T &&t) {
            this->m_value = std::forward<decltype(t)>(t);
        }

        manapi::future<T> get_return_object() {
            return manapi::future<T>{ std::coroutine_handle<promise>::from_promise(*this) };
        }

        T &&get_value() {
            return std::move(this->m_value.value());
        }

        void run_finish_cb() MANAPIHTTP_NOEXCEPT override {
            if (!this->m_finish_cb) return;
            auto ptr = this->m_value.has_value() ? &this->m_value.value() : nullptr;
            this->m_finish_cb (std::move(this->m_exception), ptr);
        }

        final_awaiter<T> final_suspend() MANAPIHTTP_NOEXCEPT { return {}; }

        std::move_only_function<void(std::exception_ptr err, T *v)> m_finish_cb;
        std::optional<T> m_value;
    };
}

namespace manapi::async {
    void coro_resume (std::coroutine_handle<> handle);

    void coro_finish (std::coroutine_handle<> handle) MANAPIHTTP_NOEXCEPT;
}

namespace manapi {
    template <typename T>
    struct onfinish_future_type {
        using type = std::move_only_function<void(std::exception_ptr, T*)>;
    };

    template <>
    struct onfinish_future_type<void> {
        using type = std::move_only_function<void(std::exception_ptr)>;
    };

    template <typename T>
    using onfinish_future_t = typename onfinish_future_type<T>::type;

    template <typename T = void>
    class future
    {
    public:
        using value_type = T;
        using promise_type = async::internal::promise<T>;

        future(std::coroutine_handle<promise_type> handle) : m_handle (handle) {}

        ~future() { this->reset(); }

        future (future &&n) MANAPIHTTP_NOEXCEPT {
            this->m_handle = std::exchange(n.m_handle, nullptr);
        }

        future &operator=(future &&n) MANAPIHTTP_NOEXCEPT {
            if (this != &n) {
                this->m_handle = std::exchange(n.m_handle, nullptr);
            }
            return *this;
        }

        void reset () {
            if (this->m_handle)
                this->m_handle.destroy();
        }

        std::coroutine_handle<promise_type> release () MANAPIHTTP_NOEXCEPT {
            return std::exchange(this->m_handle, nullptr);
        }

        void operator()() MANAPIHTTP_NOEXCEPT {
            async::coro_resume(this->m_handle);
        }

        MANAPIHTTP_NODISCARD bool operator==(const std::nullptr_t &n) const {
            return this->m_handle == nullptr;
        }

        MANAPIHTTP_NODISCARD bool operator!=(const std::nullptr_t &n) const {
            return false == this->operator==(n);
        }

        struct Awaiter {
            std::coroutine_handle<promise_type> m_handle;

            void await_suspend (std::coroutine_handle<> handle) {
                async::internal::future_awaiter_suspend(static_cast<async::internal::promise_base_future *> (&this->m_handle.promise()),
                    this->m_handle, handle);
            }

            bool await_ready () {
                return !this->m_handle || this->m_handle.done();
            }

            auto await_resume() {
                auto &p = this->m_handle.promise();

                if (p.m_exception) {
                    std::rethrow_exception(p.m_exception);
                }

                if constexpr (!std::is_same_v<T, void>) {
                    return std::move(p.get_value());
                }
            }
        };

        void onfinish (onfinish_future_t<T> cb) MANAPIHTTP_NOEXCEPT {
            if (this->m_handle) {
                auto &promise = this->m_handle.promise();
                promise.m_finish_cb = std::move(cb);
            }
        }

        MANAPIHTTP_NODISCARD bool finished () const {
            return !this->m_handle || this->m_handle.done();
        }

        MANAPIHTTP_NODISCARD const std::coroutine_handle<promise_type> &handle () {
            return this->m_handle;
        }

        Awaiter operator co_await () MANAPIHTTP_NOEXCEPT {
            return Awaiter{this->m_handle};
        }
    private:
        std::coroutine_handle<promise_type> m_handle;
    };
}

namespace manapi::async::internal {
    template<>
    class promise<void> : public promise_base_future {
    public:
        promise ();

        promise (promise &&n) MANAPIHTTP_NOEXCEPT;

        ~promise() override;

        void return_void ();

        void run_finish_cb() MANAPIHTTP_NOEXCEPT override;

        final_awaiter<void> final_suspend() MANAPIHTTP_NOEXCEPT;

        future<void> get_return_object();

        std::move_only_function<void(std::exception_ptr err)> m_finish_cb;
    };
}
