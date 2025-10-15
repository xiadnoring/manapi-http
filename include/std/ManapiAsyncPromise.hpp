#pragma once

#include <functional>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async::internal {
    enum promise_flags {
        PROMISE_FLAG_EXECUTED = 1,
        PROMISE_FLAG_ASYNC = 2,
        PROMISE_FLAG_RESOLVE_ONLY = 4
    };

    template<typename T = void>
    struct promise_data_t {
        std::exception_ptr exception{nullptr};
        char flags;
        std::optional <T> value;
        void *cb;
        std::coroutine_handle<> handle;

        ~promise_data_t();
    };


    template<>
    struct promise_data_t<void> {
        std::exception_ptr exception{nullptr};
        char flags;
        void *cb;
        std::coroutine_handle<> handle;

        ~promise_data_t();
    };

    template<typename T>
    void promise_call (std::shared_ptr<promise_data_t<T>> data) MANAPIHTTP_NOEXCEPT {
        auto handle = std::exchange(data->handle, nullptr);
        MANAPIHTTP_MUST_ALLOC_START
        manapi::async::internal::append_static_task([handle] ()
            -> void {
            handle.resume();
        });
        MANAPIHTTP_MUST_ALLOC_END
    }

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void call_promise_resolve (std::shared_ptr<promise_data_t<T>> data, T &v) MANAPIHTTP_NOEXCEPT {
        if ((data->flags & PROMISE_FLAG_EXECUTED) /* ready */) {
            return;
        }
        data->flags |= PROMISE_FLAG_EXECUTED;
        data->value = std::move(v);
        promise_call(std::move(data));
    }

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void call_promise_reject (std::shared_ptr<promise_data_t<T>> data, std::exception_ptr e) MANAPIHTTP_NOEXCEPT {
        if ((data->flags & PROMISE_FLAG_EXECUTED) /* ready */) {
            return;
        }
        data->flags |= PROMISE_FLAG_EXECUTED;
        data->exception = std::move(e);
        promise_call(std::move(data));
    }

    template<typename T>
    requires(std::is_same_v<T, void>)
    void call_promise_resolve (std::shared_ptr<promise_data_t<T>> data) MANAPIHTTP_NOEXCEPT {
        if ((data->flags & PROMISE_FLAG_EXECUTED) /* ready */) {
            return;
        }
        data->flags |= PROMISE_FLAG_EXECUTED;
        promise_call(std::move(data));
    }

    template<typename T>
    requires(std::is_same_v<T, void>)
    void call_promise_reject (std::shared_ptr<promise_data_t<T>> data, std::exception_ptr e) MANAPIHTTP_NOEXCEPT {
        if ((data->flags & PROMISE_FLAG_EXECUTED) /* ready */) {
            return;
        }
        data->flags |= PROMISE_FLAG_EXECUTED;
        data->exception = std::move(e);
        promise_call(std::move(data));
    }
}

namespace manapi::async {
    template<typename T = void>
    class promise_resolve {
    public:
        promise_resolve () : data_(nullptr) {}

        promise_resolve (std::shared_ptr<internal::promise_data_t<T>> data) {
            this->data_ = std::move(data);
        }

        MANAPIHTTP_NODISCARD operator bool () MANAPIHTTP_NOEXCEPT { return !!this->data_; }

        MANAPIHTTP_NODISCARD operator bool () const MANAPIHTTP_NOEXCEPT { return !!this->data_; }

        promise_resolve (promise_resolve &&n) MANAPIHTTP_NOEXCEPT = default;

        promise_resolve &operator=(promise_resolve &&n) MANAPIHTTP_NOEXCEPT = default;

        promise_resolve (const promise_resolve &n) = default;

        promise_resolve&operator= (const promise_resolve &n) = default;

        void operator () (T v) MANAPIHTTP_NOEXCEPT {
            internal::call_promise_resolve<T> (this->data_, v);
        }

        void operator () (T v) const MANAPIHTTP_NOEXCEPT {
            internal::call_promise_resolve<T> (this->data_, v);
        }
    private:
        std::shared_ptr<internal::promise_data_t<T>> data_;
    };

    template<>
    class promise_resolve<void> {
    public:
        promise_resolve () : data_(nullptr) {}

        promise_resolve (std::shared_ptr<internal::promise_data_t<void>> data) {
            this->data_ = std::move(data);
        }

        MANAPIHTTP_NODISCARD operator bool () MANAPIHTTP_NOEXCEPT { return !!this->data_; }

        MANAPIHTTP_NODISCARD operator bool () const MANAPIHTTP_NOEXCEPT { return !!this->data_; }

        promise_resolve (promise_resolve &&n) MANAPIHTTP_NOEXCEPT = default;

        promise_resolve &operator=(promise_resolve &&n) MANAPIHTTP_NOEXCEPT = default;

        promise_resolve (const promise_resolve &n) = default;

        promise_resolve&operator= (const promise_resolve &n) = default;

        void operator () () MANAPIHTTP_NOEXCEPT {
            internal::call_promise_resolve<void> (this->data_);
        }

        void operator () () const MANAPIHTTP_NOEXCEPT {
            internal::call_promise_resolve<void> (this->data_);
        }
    private:
        std::shared_ptr<internal::promise_data_t<void>> data_;
    };

    template<typename T = void>
    class promise_reject {
    public:
        promise_reject () : data_(nullptr) {}

        promise_reject (std::shared_ptr<internal::promise_data_t<T>> data) {
            this->data_ = std::move(data);
        }

        MANAPIHTTP_NODISCARD operator bool () MANAPIHTTP_NOEXCEPT { return !!this->data_; }

        MANAPIHTTP_NODISCARD operator bool () const MANAPIHTTP_NOEXCEPT { return !!this->data_; }

        promise_reject (promise_reject &&n) MANAPIHTTP_NOEXCEPT = default;

        promise_reject &operator=(promise_reject &&n) MANAPIHTTP_NOEXCEPT = default;

        promise_reject (const promise_reject &n) = default;

        promise_reject&operator= (const promise_reject &n) = default;

        void operator () (std::exception_ptr err) MANAPIHTTP_NOEXCEPT {
            internal::call_promise_reject(this->data_, std::move(err));
        }

        void operator () (std::exception_ptr err) const MANAPIHTTP_NOEXCEPT {
            internal::call_promise_reject(this->data_, std::move(err));
        }
    private:
        std::shared_ptr<internal::promise_data_t<T>> data_;
    };
}

namespace manapi::async::internal {
    template<typename T, typename Async = std::true_type>
    class promise_base {
    public:
        typedef manapi::async::promise_resolve<T> resolve_t;
        typedef manapi::async::promise_reject<T> reject_t;
        typedef std::move_only_function<future<>(resolve_t, reject_t)> async_cb;
        typedef std::move_only_function<void(resolve_t, reject_t)> sync_cb;
        typedef std::move_only_function<future<>(resolve_t)> async_resolve_cb;
        typedef std::move_only_function<void(resolve_t)> sync_resolve_cb;
        typedef promise_data_t<T> data_t;

        typedef const resolve_t& resolve_ref_t;
        typedef const reject_t& reject_ref_t;

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

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise_base(async_resolve_cb cb) {
            this->data = std::make_shared<data_t>(nullptr, 0, std::optional <T>{}, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->flags |= PROMISE_FLAG_ASYNC|PROMISE_FLAG_RESOLVE_ONLY; this->data->cb = obj.release(); }
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise_base(sync_resolve_cb cb) {
            this->data = std::make_shared<data_t>(nullptr, 0, std::optional <T>{}, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->flags |= PROMISE_FLAG_RESOLVE_ONLY; this->data->cb = obj.release(); }
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
            try {
                this->data->handle = handle;
                if (this->data->flags & PROMISE_FLAG_ASYNC /* async */) {
                    if (this->data->flags & PROMISE_FLAG_RESOLVE_ONLY) {
                        async::run<void>(static_cast<async_resolve_cb *>(this->data->cb)->operator()(promise_resolve<T>(this->data)),
                            [data = this->data] (std::exception_ptr e) -> void {
                                if (e) {
                                    call_promise_reject(data, std::move(e));
                                }
                            });
                    }
                    else {
                        async::run<void>(static_cast<async_cb *>(this->data->cb)->operator()(
                            promise_resolve<T>(this->data),
                            promise_reject<T>(this->data)),
                            [data = this->data] (std::exception_ptr e) -> void {
                                if (e) {
                                    call_promise_reject(data, std::move(e));
                                }
                            });
                    }
                }
                else {
                    if (this->data->flags & PROMISE_FLAG_RESOLVE_ONLY) {
                        static_cast<sync_resolve_cb *>(this->data->cb)->operator() (promise_resolve<T>(this->data));
                    }
                    else {
                        static_cast<sync_cb *>(this->data->cb)->operator() (
                            promise_resolve<T>(this->data),
                            promise_reject<T>(this->data));
                    }
                }
            }
            catch (std::exception const &e) {
                manapi_log_error(e.what());
                call_promise_reject(this->data, std::current_exception());
            }
        }
    private:
        std::shared_ptr<data_t> data;
    };

    template<typename Async>
    class promise_base<void, Async> {
    public:
        typedef manapi::async::promise_resolve<void> resolve_t;
        typedef manapi::async::promise_reject<void> reject_t;

        typedef std::move_only_function<future<>(resolve_t, reject_t)> async_cb;
        typedef std::move_only_function<void(resolve_t, reject_t)> sync_cb;

        typedef std::move_only_function<future<>(resolve_t)> async_resolve_cb;
        typedef std::move_only_function<void(resolve_t)> sync_resolve_cb;

        typedef const resolve_t& resolve_ref_t;
        typedef const reject_t& reject_ref_t;

        typedef promise_data_t<void> data_t;

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


        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::true_type>)
        promise_base(async_resolve_cb cb) {
            this->data = std::make_shared<data_t>(nullptr, 0, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->flags |= PROMISE_FLAG_ASYNC|PROMISE_FLAG_RESOLVE_ONLY; this->data->cb = obj.release(); }
        }

        template<typename Async1 = Async>
        requires(std::is_same_v<Async1, std::false_type>)
        promise_base(sync_resolve_cb cb) {
            this->data = std::make_shared<data_t>(nullptr, 0, nullptr);
            if (cb) { auto obj = std::make_unique<decltype(cb)>(std::move(cb)); this->data->flags |= PROMISE_FLAG_RESOLVE_ONLY; this->data->cb = obj.release(); }
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
            try {
                this->data->handle = handle;
                if (this->data->flags & PROMISE_FLAG_ASYNC /* async */) {
                    if (this->data->flags & PROMISE_FLAG_RESOLVE_ONLY) {
                        async::run<void>(static_cast<async_resolve_cb *>(this->data->cb)->operator()(
                            promise_resolve<void>(this->data)),
                        [data = this->data] (std::exception_ptr e) -> void {
                            if (e) {
                                call_promise_reject(data, std::move(e));
                            }
                        });
                    }
                    else {
                        async::run<void>(static_cast<async_cb *>(this->data->cb)->operator()(
                            promise_resolve<void>(this->data),
                            promise_reject<void>(this->data)),
                            [data = this->data] (std::exception_ptr e) -> void {
                                if (e) {
                                    call_promise_reject(data, std::move(e));
                                }
                            });
                    }
                }
                else {
                    if (this->data->flags & PROMISE_FLAG_RESOLVE_ONLY) {
                        static_cast<sync_resolve_cb *>(this->data->cb)->operator() (promise_resolve<>(this->data));
                    }
                    else {
                        static_cast<sync_cb *>(this->data->cb)->operator() (
                            promise_resolve<>(this->data),
                            promise_reject<>(this->data));
                    }
                }
            }
            catch (std::exception const &e) {
                manapi_log_error(e.what());
                call_promise_reject(this->data, std::current_exception());
            }
        }
    private:
        std::shared_ptr<data_t> data{nullptr};
    };


    template<typename T>
    promise_data_t<T>::~promise_data_t() {
        if (this->flags & PROMISE_FLAG_ASYNC) { delete static_cast<typename promise_base<T>::async_cb *> (this->cb); }
        else { delete static_cast<typename promise_base<T>::sync_cb *> (this->cb); }
    }

    inline promise_data_t<>::~promise_data_t() {
        if (this->flags & PROMISE_FLAG_ASYNC) { delete static_cast<typename promise_base<void>::async_cb *> (this->cb); }
        else { delete static_cast<typename promise_base<void>::sync_cb *> (this->cb); }
    }
}

namespace manapi::async {
    template<typename T>
    using promise_sync = internal::promise_base<T, std::false_type>;

    template<typename T>
    using promise_async = internal::promise_base<T, std::true_type>;
}
