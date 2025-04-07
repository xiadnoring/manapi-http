#pragma once

#include <functional>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    template<typename T, typename Async = std::true_type>
    class promise {
    public:
        typedef std::function<void(T v)> resolve_t;
        typedef std::function<void(std::exception_ptr )> reject_t;

        typedef const resolve_t& resolve_ref_t;
        typedef const reject_t& reject_ref_t;

        struct data_t {
            std::shared_ptr<threadpool<task>> taskpool{nullptr};
            std::move_only_function<void(resolve_t, reject_t)> cb{nullptr};
            std::move_only_function<future<>(resolve_t, reject_t)> async_cb{nullptr};
            std::exception_ptr exception{nullptr};
            std::unique_ptr<std::atomic<bool>> ready;
            std::optional <T> value;
        };

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise(const std::shared_ptr<context> &ctx, std::move_only_function<future<>(resolve_t, reject_t)> cb) {
            this->data = std::make_shared<data_t>(data_t{as_threadpool(ctx), nullptr, std::move(cb), nullptr, nullptr, {}});
            this->data->ready = std::make_unique<std::atomic<bool>>(false);
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise(std::shared_ptr<threadpool<task>> taskpool, std::move_only_function<future<>(resolve_t, reject_t)> cb) {
            this->data = std::make_shared<data_t>(data_t{std::move(taskpool), nullptr, std::move(cb), nullptr, nullptr, {}});
            this->data->ready = std::make_unique<std::atomic<bool>>(false);
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise(const std::shared_ptr<context> &ctx, std::move_only_function<void(resolve_t, reject_t)> cb) {
            this->data = std::make_shared<data_t>(data_t{as_threadpool(ctx), std::move(cb), nullptr, nullptr});
            this->data->ready = std::make_unique<std::atomic<bool>>(false);
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise(const std::shared_ptr<threadpool<task>> &taskpool, std::move_only_function<void(resolve_t, reject_t)> cb) {
            this->data = std::make_shared<data_t>(data_t{taskpool, std::move(cb), nullptr, nullptr});
            this->data->ready = std::make_unique<std::atomic<bool>>(false);
        }

        ~promise() = default;

        T await_resume () {
            if (this->data->exception) {
                std::rethrow_exception(std::move(this->data->exception));
            }

            return std::move(this->data->value.value());
        }

        bool await_ready () {
            return false;
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            if (this->data->cb) {
                this->data->cb ([handle, data = this->data] (T v) mutable
                    -> void { resolve(std::move(data), handle, v); },
                    [handle, data = this->data] (std::exception_ptr e) mutable
                    -> void { reject(std::move(data), handle, std::move(e)); });
            }
            else {
                async::run(this->data->taskpool, this->data->async_cb ([handle, data = this->data] (T v) mutable
                    -> void { resolve(std::move(data), handle, v); },
                    [handle, data = this->data] (std::exception_ptr e) mutable
                    -> void { reject(std::move(data), handle, std::move(e)); }), [data = this->data] () -> void {});
            }
        }
    private:
        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        static void call (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle) {
            data->taskpool->append_task([handle = std::exchange(handle, nullptr)] ()
                 -> void { handle.resume(); });
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        static void resolve (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle, T &v) {
            if (data->ready->exchange(true)) {
                return;
            }
            data->value = std::move(v);
            call(std::move(data), handle);
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        static void reject (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle, std::exception_ptr e) {
            if (data->ready->exchange(true)) {
                return;
            }
            data->exception = std::move(e);
            call(std::move(data), handle);
        }

        std::shared_ptr<data_t> data;
    };


    template<typename Async>
    class promise<void, Async> {
    public:
        typedef std::function<void()> resolve_t;
        typedef std::function<void(std::exception_ptr )> reject_t;

        typedef const resolve_t& resolve_ref_t;
        typedef const reject_t& reject_ref_t;

        struct data_t {
            std::shared_ptr<threadpool<task>> taskpool{nullptr};
            std::move_only_function<void(resolve_t, reject_t)> cb{nullptr};
            std::move_only_function<future<>(resolve_t, reject_t)> async_cb{nullptr};
            std::exception_ptr exception{nullptr};
            std::unique_ptr<std::atomic<bool>> ready;
        };


        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise(const std::shared_ptr<context> &ctx, std::move_only_function<future<>(resolve_t, reject_t)> cb) {
            this->data = std::make_shared<data_t>(data_t{as_threadpool(ctx), nullptr, std::move(cb), nullptr, nullptr});
            this->data->ready = std::make_unique<std::atomic<bool>>(false);
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise(std::shared_ptr<threadpool<task>> taskpool, std::move_only_function<future<>(resolve_t, reject_t)> cb) {
            this->data = std::make_shared<data_t>(data_t{std::move(taskpool), nullptr, std::move(cb), nullptr, nullptr});
            this->data->ready = std::make_unique<std::atomic<bool>>(false);
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise(const std::shared_ptr<context> &ctx, std::move_only_function<void(resolve_t, reject_t)> cb) {
            this->data = std::make_shared<data_t>(data_t{as_threadpool(ctx), std::move(cb), nullptr, nullptr});
            this->data->ready = std::make_unique<std::atomic<bool>>(false);
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise(const std::shared_ptr<threadpool<task>> &taskpool, std::move_only_function<void(resolve_t, reject_t)> cb) {
            this->data = std::make_shared<data_t>(data_t{taskpool, std::move(cb), nullptr, nullptr});
            this->data->ready = std::make_unique<std::atomic<bool>>(false);
        }

        ~promise() = default;

        void await_resume () {
            if (this->data->exception) {
                std::rethrow_exception(std::move(this->data->exception));
            }
        }

        bool await_ready () {
            return false;
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            if (this->data->cb) {
                this->data->cb ([data = this->data, handle] () mutable
                    -> void { resolve(std::move(data), handle); },
                [data = this->data, handle] (std::exception_ptr e) mutable
                    -> void { reject(std::move(data), handle, std::move(e)); });
            }
            else {
                async::run(this->data->taskpool, this->data->async_cb ([data = this->data, handle] () mutable
                    -> void { resolve(std::move(data), handle); },
                [data = this->data, handle] (std::exception_ptr e) mutable
                    -> void { reject(std::move(data), handle, std::move(e)); }), [data = this->data] () -> void {});
            }
        }
    private:
        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        static void call (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle) {
            data->taskpool->append_task([handle = std::exchange(handle, nullptr)] ()
                -> void { handle.resume(); });
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        static void resolve (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle) {
            if (data->ready->exchange(true)) {
                return;
            }
            call(std::move(data), handle);
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        static void reject (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle, std::exception_ptr e) {
            if (data->ready->exchange(true)) {
                return;
            }
            data->exception = std::move(e);
            call(std::move(data), handle);
        }

    private:
        std::shared_ptr<data_t> data{nullptr};
    };
}
