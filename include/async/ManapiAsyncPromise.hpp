#pragma once

#include <functional>

#include "ManapiAsync.hpp"
#include "ManapiAsyncContext.hpp"

namespace manapi::async {
    template<typename T>
    class promise {
    public:
        typedef std::function<void(T v)> resolve_t;
        typedef std::function<void(std::exception_ptr )> reject_t;

        typedef const resolve_t& resolve_ref_t;
        typedef const reject_t& reject_ref_t;

        explicit promise(const std::shared_ptr<context> &ctx, const std::function<future<>(resolve_ref_t, reject_ref_t)> &cb) {
            this->async_cb = cb;
            this->taskpool = as_threadpool(ctx);
        }

        explicit promise(const std::shared_ptr<threadpool<task>> &taskpool, const std::function<future<>(resolve_ref_t, reject_ref_t)> &cb) {
            this->async_cb = cb;
            this->taskpool = taskpool;
        }

        ~promise() = default;

        T await_resume () {
            if (this->exception) {
                std::rethrow_exception(this->exception);
            }

            return this->value.value();
        }

        bool await_ready () {
            return false;
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            if (this->cb) {
                this->cb ([this, handle = std::exchange(handle, nullptr)] (T v) mutable -> void {
                    this->resolve(handle, v);
                }, [this, handle] (std::exception_ptr e) {
                    this->reject(handle, e);
                });
            }
            else {
                async::run(this->taskpool, this->async_cb ([this, handle = std::exchange(handle, nullptr)] (T v) mutable -> void {
                    this->resolve(handle, v);
                }, [this, handle] (std::exception_ptr e) {
                    this->reject(handle, e);
                }));
            }
        }
    private:
        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void call (std::coroutine_handle<T1> handle) {
            this->taskpool->append_task([handle = std::exchange(handle, nullptr)]()
                mutable -> void { handle(); } );
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void resolve (std::coroutine_handle<T1> handle, T &v) {
            this->value = std::move(v);
            this->call(handle);
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void reject (std::coroutine_handle<T1> handle, const std::exception_ptr &e) {
            this->exception = e;
            this->call(handle);
        }

        std::exception_ptr exception{nullptr};
        std::shared_ptr<threadpool<task>> taskpool{nullptr};
        std::function<void(resolve_ref_t, reject_ref_t)> cb{nullptr};
        std::function<future<>(resolve_ref_t, reject_ref_t)> async_cb{nullptr};
        std::optional<T> value;
    };


    template<>
    class promise<void> {
    public:
        typedef std::function<void()> resolve_t;
        typedef std::function<void(std::exception_ptr )> reject_t;

        typedef const resolve_t& resolve_ref_t;
        typedef const reject_t& reject_ref_t;

        explicit promise(const std::shared_ptr<context> &ctx, const std::function<future<>(resolve_ref_t, reject_ref_t)> &cb) {
            this->async_cb = cb;
            this->taskpool = as_threadpool(ctx);
        }

        explicit promise(const std::shared_ptr<threadpool<task>> &taskpool, const std::function<future<>(resolve_ref_t, reject_ref_t)> &cb) {
            this->async_cb = cb;
            this->taskpool = taskpool;
        }

        ~promise() = default;

        void await_resume () {
            if (this->exception) {
                std::rethrow_exception(this->exception);
            }
        }

        bool await_ready () {
            return false;
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            if (this->cb) {
                this->cb ([this, handle = std::exchange(handle, nullptr)] () mutable -> void {
                    this->resolve(handle);
                }, [this, handle] (std::exception_ptr e) {
                    this->reject(handle, e);
                });
            }
            else {
                async::run(this->taskpool, this->async_cb ([this, handle] () mutable -> void {
                    this->resolve(handle);
                }, [this, handle] (std::exception_ptr e) {
                    this->reject(handle, e);
                }));
            }
        }
    private:
        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void call (std::coroutine_handle<T1> handle) {
            this->taskpool->append_task([handle]()
                mutable -> void { handle(); } );
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void resolve (std::coroutine_handle<T1> handle) {
            this->call(handle);
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void reject (std::coroutine_handle<T1> handle, const std::exception_ptr &e) {
            this->exception = e;
            this->call(handle);
        }

        std::exception_ptr exception{nullptr};
        std::shared_ptr<threadpool<task>> taskpool{nullptr};
        std::function<void(resolve_ref_t, reject_ref_t)> cb{nullptr};
        std::function<future<>(resolve_ref_t, reject_ref_t)> async_cb{nullptr};
    };
}
