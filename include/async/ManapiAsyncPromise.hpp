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
        typedef std::move_only_function<future<>(resolve_t, reject_t)> async_cb;
        typedef std::move_only_function<void(resolve_t, reject_t)> sync_cb;

        typedef const resolve_t& resolve_ref_t;
        typedef const reject_t& reject_ref_t;

        struct data_t {
            std::shared_ptr<threadpool<task>> taskpool{nullptr};
            std::exception_ptr exception{nullptr};
            std::atomic<int> flags;
            std::optional <T> value;
            void *cb;

            ~data_t() {
                if (this->flags & 0b10) { delete static_cast<async_cb *> (this->cb); }
                else { delete static_cast<sync_cb *> (this->cb); }
            }
        };

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise(shared_cthread ctx, async_cb cb) {
            this->data = std::make_shared<data_t>(internal::ethreadpool_(ctx), nullptr, 0, std::optional <T>{}, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->flags.store(0b10); this->data->cb = obj.release(); }
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise(shared_taskpool taskpool, async_cb cb) {
            this->data = std::make_shared<data_t>(std::move(taskpool), nullptr, 0, std::optional <T>{}, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->flags.store(0b10); this->data->cb = obj.release(); }
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise(shared_cthread ctx, sync_cb cb) {
            this->data = std::make_shared<data_t>(internal::ethreadpool_(ctx), nullptr, 0, std::optional <T>{}, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->cb = obj.release(); }
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise(shared_taskpool taskpool, sync_cb cb) {
            this->data = std::make_shared<data_t>(std::move(taskpool), nullptr, 0, std::optional <T>{}, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->cb = obj.release(); }
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
            if (this->data->flags & 0b10 /* async */) {
                async::run<void>(this->data->taskpool, static_cast<async_cb *>(this->data->cb)->operator()([handle, data = this->data] (T v) mutable
                    -> void { resolve(std::move(data), handle, v); },
                    [handle, data = this->data] (std::exception_ptr e) mutable
                    -> void { reject(std::move(data), handle, std::move(e)); }), [data = this->data] (std::exception_ptr err) -> void {});
            }
            else {
                static_cast<sync_cb *>(this->data->cb)->operator() ([handle, data = this->data] (T v) mutable
                    -> void { resolve(std::move(data), handle, v); },
                    [handle, data = this->data] (std::exception_ptr e) mutable
                    -> void { reject(std::move(data), handle, std::move(e)); });
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
            if ((data->flags.fetch_or(0b1) & 0b1) /* ready */) {
                return;
            }
            data->value = std::move(v);
            call(std::move(data), handle);
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        static void reject (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle, std::exception_ptr e) {
            if ((data->flags.fetch_or(0b1) & 0b1) /* ready */) {
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

        typedef std::move_only_function<future<>(resolve_t, reject_t)> async_cb;
        typedef std::move_only_function<void(resolve_t, reject_t)> sync_cb;

        typedef const resolve_t& resolve_ref_t;
        typedef const reject_t& reject_ref_t;

        struct data_t {
            std::shared_ptr<threadpool<task>> taskpool{nullptr};
            std::exception_ptr exception{nullptr};
            std::atomic<int> flags;
            void *cb;

            ~data_t() {
                if (this->flags & 0b10) { delete static_cast<async_cb *> (this->cb); }
                else { delete static_cast<sync_cb *> (this->cb); }
            }
        };


        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise(shared_cthread ctx, async_cb cb) {
            this->data = std::make_shared<data_t>(internal::ethreadpool_(ctx), nullptr, 0, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->flags.store(0b10); this->data->cb = obj.release(); }
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise(shared_taskpool taskpool, async_cb cb) {
            this->data = std::make_shared<data_t>(std::move(taskpool), nullptr, 0, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->flags.store(0b10); this->data->cb = obj.release(); }
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise(shared_cthread ctx, sync_cb cb) {
            this->data = std::make_shared<data_t>(internal::ethreadpool_(ctx), nullptr, 0, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->cb = obj.release(); }
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise(shared_taskpool taskpool, sync_cb cb) {
            this->data = std::make_shared<data_t>(std::move(taskpool), nullptr, 0, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->cb = obj.release(); }
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
            if ((this->data->flags & 0b10)) {
                async::run<void>(this->data->taskpool, static_cast<async_cb *>(this->data->cb)->operator() ([data = this->data, handle] () mutable
                    -> void { resolve(std::move(data), handle); },
                [data = this->data, handle] (std::exception_ptr e) mutable
                    -> void { reject(std::move(data), handle, std::move(e)); }), [data = this->data] (std::exception_ptr err) -> void {});
            }
            else {
                static_cast<sync_cb *>(this->data->cb)->operator() ([data = this->data, handle] () mutable
                    -> void { resolve(std::move(data), handle); },
                [data = this->data, handle] (std::exception_ptr e) mutable
                    -> void { reject(std::move(data), handle, std::move(e)); });
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
            if ((data->flags.fetch_or(0b1) & 0b1) /* ready */) {
                return;
            }
            call(std::move(data), handle);
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        static void reject (std::shared_ptr<data_t> data, std::coroutine_handle<T1> handle, std::exception_ptr e) {
            if ((data->flags.fetch_or(0b1) & 0b1) /* ready */) {
                return;
            }
            data->exception = std::move(e);
            call(std::move(data), handle);
        }

    private:
        std::shared_ptr<data_t> data{nullptr};
    };
}
