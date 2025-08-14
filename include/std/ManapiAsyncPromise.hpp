#pragma once

#include <functional>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async::internal {
    enum promise_flags {
        PROMISE_FLAG_EXECUTED = 1,
        PROMISE_FLAG_ASYNC = 2
    };

    template<typename T, typename Async = std::true_type>
    class promise_base {
    public:
        typedef std::function<void(T v)> resolve_t;
        typedef std::function<void(std::exception_ptr )> reject_t;
        typedef std::move_only_function<future<>(resolve_t, reject_t)> async_cb;
        typedef std::move_only_function<void(resolve_t, reject_t)> sync_cb;

        typedef const resolve_t& resolve_ref_t;
        typedef const reject_t& reject_ref_t;

        struct data_t {
            std::exception_ptr exception{nullptr};
            char flags;
            std::optional <T> value;
            void *cb;

            ~data_t() {
                if (this->flags & PROMISE_FLAG_ASYNC) { delete static_cast<async_cb *> (this->cb); }
                else { delete static_cast<sync_cb *> (this->cb); }
            }
        };

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise_base(async_cb cb) {
            this->data = std::make_shared<data_t>(nullptr, 0, std::optional <T>{}, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->flags |= PROMISE_FLAG_ASYNC; this->data->cb = obj.release(); }
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise_base(sync_cb cb) {
            this->data = std::make_shared<data_t>(nullptr, 0, std::optional <T>{}, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->cb = obj.release(); }
        }

        ~promise_base() = default;

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
        requires(std::is_base_of_v<promise_base_future, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            if (this->data->flags & PROMISE_FLAG_ASYNC /* async */) {
                async::run<void>(static_cast<async_cb *>(this->data->cb)->operator()([data = this->data, handle] (T v) mutable
                    -> void { resolve((data), handle, v); },
                    [data = this->data, handle] (std::exception_ptr e) mutable
                    -> void { reject((data), handle, std::move(e)); }),
                    [data = this->data, handle] (std::exception_ptr e) -> void {
                        if (e)
                            reject(data, handle, std::current_exception());
                    });
            }
            else {
                try {
                    static_cast<sync_cb *>(this->data->cb)->operator() ([data = this->data, handle] (T v) mutable
                        -> void { resolve(data, handle, v); },
                        [data = this->data, handle] (std::exception_ptr e) mutable
                        -> void { reject((data), handle, std::move(e)); });
                }
                catch (std::exception const &e) {
                    manapi_log_error(e.what());
                    reject(data, handle, std::current_exception());
                }
            }
        }
    private:
        template <typename T1>
        requires(std::is_base_of_v<promise_base_future, T1>)
        static void call (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle) MANAPIHTTP_NOEXCEPT {
            MANAPIHTTP_MUST_ALLOC_START
            manapi::async::internal::ethreadpool_(manapi::async::current())->append_task([handle] ()
                -> void {
                handle.resume();
            });
            MANAPIHTTP_MUST_ALLOC_END
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base_future, T1>)
        static void resolve (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle, T &v) MANAPIHTTP_NOEXCEPT {
            if ((data->flags & PROMISE_FLAG_EXECUTED) /* ready */) {
                return;
            }
            data->flags |= PROMISE_FLAG_EXECUTED;
            data->value = std::move(v);
            call(std::move(data), handle);
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base_future, T1>)
        static void reject (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle, std::exception_ptr e) MANAPIHTTP_NOEXCEPT {
            if ((data->flags & PROMISE_FLAG_EXECUTED) /* ready */) {
                return;
            }
            data->flags |= PROMISE_FLAG_EXECUTED;
            data->exception = std::move(e);
            call(std::move(data), handle);
        }

        std::shared_ptr<data_t> data;
    };


    template<typename Async>
    class promise_base<void, Async> {
    public:
        typedef std::function<void()> resolve_t;
        typedef std::function<void(std::exception_ptr )> reject_t;

        typedef std::move_only_function<future<>(resolve_t, reject_t)> async_cb;
        typedef std::move_only_function<void(resolve_t, reject_t)> sync_cb;

        typedef const resolve_t& resolve_ref_t;
        typedef const reject_t& reject_ref_t;

        struct data_t {
            std::exception_ptr exception{nullptr};
            std::atomic<int> flags;
            void *cb;

            ~data_t() {
                if (this->flags & PROMISE_FLAG_ASYNC) { delete static_cast<async_cb *> (this->cb); }
                else { delete static_cast<sync_cb *> (this->cb); }
            }
        };

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise_base(async_cb cb) {
            this->data = std::make_shared<data_t>(nullptr, 0, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->flags |= PROMISE_FLAG_ASYNC; this->data->cb = obj.release(); }
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise_base(sync_cb cb) {
            this->data = std::make_shared<data_t>(nullptr, 0, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->cb = obj.release(); }
        }

        ~promise_base() = default;

        void await_resume () {
            if (this->data->exception) {
                std::rethrow_exception(std::move(this->data->exception));
            }
        }

        bool await_ready () {
            return false;
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base_future, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            if (this->data->flags & PROMISE_FLAG_ASYNC /* async */) {
                async::run<void>(static_cast<async_cb *>(this->data->cb)->operator()([data = this->data, handle] () mutable
                    -> void { resolve((data), handle); },
                    [data = this->data, handle] (std::exception_ptr e) mutable
                    -> void { reject((data), handle, std::move(e)); }));
            }
            else {
                try {
                    static_cast<sync_cb *>(this->data->cb)->operator() ([data = this->data, handle] () mutable
                        -> void { resolve(data, handle); },
                        [data = this->data, handle] (std::exception_ptr e) mutable
                        -> void { reject((data), handle, std::move(e)); });
                }
                catch (std::exception const &e) {
                    manapi_log_error(e.what());
                    reject(data, handle, std::current_exception());
                }
            }
        }
    private:
        template <typename T1>
        requires(std::is_base_of_v<promise_base_future, T1>)
        static void call (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle) MANAPIHTTP_NOEXCEPT {
            MANAPIHTTP_MUST_ALLOC_START
            manapi::async::internal::ethreadpool_(manapi::async::current())->append_task([handle] ()
                -> void {
                handle.resume();
            });
            MANAPIHTTP_MUST_ALLOC_END
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base_future, T1>)
        static void resolve (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle) MANAPIHTTP_NOEXCEPT {
            if ((data->flags & PROMISE_FLAG_EXECUTED) /* ready */) {
                return;
            }
            data->flags |= PROMISE_FLAG_EXECUTED;
            call(std::move(data), handle);
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base_future, T1>)
        static void reject (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle, std::exception_ptr e) MANAPIHTTP_NOEXCEPT {
            if ((data->flags & PROMISE_FLAG_EXECUTED) /* ready */) {
                return;
            }
            data->flags |= PROMISE_FLAG_EXECUTED;
            data->exception = std::move(e);
            call(std::move(data), handle);
        }

    private:
        std::shared_ptr<data_t> data{nullptr};
    };
}

namespace manapi::async {
    template<typename T>
    using promise_sync = internal::promise_base<T, std::false_type>;

    template<typename T>
    using promise_async = internal::promise_base<T, std::true_type>;
}
