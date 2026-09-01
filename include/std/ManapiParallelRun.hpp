#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiMutex.hpp"
#include "./ManapiContext.hpp"
#include "./std/ManapiRef.hpp"
#include "./std/ManapiPromise.hpp"

namespace manapi::async {
    struct parallel_t {
        using promise_z = manapi::async::promise_async<void>;

        ~parallel_t() {
            if (this->resolve) {
                this->resolve();
            }
        }

        promise_z::resolve_t resolve;
        uint32_t refcnt;
    };

    template<typename T = void>
    class parallel_run : public std::enable_shared_from_this<parallel_run<T>>{
    public:
        struct value_t {
        public:
            manapi::future<manapi::status> run (manapi::future<T> task) {
                try {
                    this->value = (co_await task);
                    co_return manapi::status_ok();
                }
                catch (std::exception const &e) {
                    manapi_log_trace("%s due to %s", "parallel_run:Failed", e.what());
                    co_return manapi::status_internal("parallel_run:Failed");
                }
            }

            MANAPIHTTP_NODISCARD bool some () const MANAPIHTTP_NOEXCEPT {
                return this->value.has_value();
            }

            manapi::status_or<T> get () {
                if (this->value.has_value()) {
                    auto val = std::move(this->value.value());
                    this->value.reset();
                    return std::move(val);
                }
                return manapi::status_not_found("parallel_run:no data");
            }

            T get_or (T &&v) {
                if (this->some())
                    return this->get().unwrap();

                return std::forward<decltype(v)>(v);
            }
        private:
            std::optional<T> value;
        };

        parallel_run (manapi::reference<parallel_t> st)  {
            this->st = std::move(st);
        }

        static manapi::status_or<std::shared_ptr<parallel_run>> create (manapi::reference<parallel_t> st) MANAPIHTTP_NOEXCEPT {
            try {
                return std::make_shared<parallel_run>(std::move(st));
            }
            catch (std::exception const &) {
                return manapi::status_resource_exhausted();
            }
        }

        static manapi::status_or<std::shared_ptr<parallel_run>> create (manapi::reference<parallel_t> st, manapi::future<T> task) MANAPIHTTP_NOEXCEPT {
            try {
                auto z = std::make_shared<parallel_run>(std::move(st));
                z->run(std::move(task));
                return std::move(z);
            }
            catch (std::exception const &e) {
                return manapi::status_resource_exhausted();
            }
        }

        ~parallel_run () = default;

        manapi::status run (manapi::future<T> task);

        MANAPIHTTP_NODISCARD manapi::future<manapi::status_or<T>> get () {
            auto lk = co_await this->mx.lock_guard();
            co_return this->value.get();
        }

        MANAPIHTTP_NODISCARD manapi::future<T> get_or (T &&v) {
            auto lk = co_await this->mx.lock_guard();
            co_return this->value.get_or(std::forward<decltype(v)>(v));
        }

        MANAPIHTTP_NODISCARD manapi::future<T> get_or (const T &v) {
            auto lk = co_await this->mx.lock_guard();
            co_return this->value.get_or(T (v));
        }

        MANAPIHTTP_NODISCARD bool some () const MANAPIHTTP_NOEXCEPT {
            return this->value.some();
        }
    private:
        manapi::async::mutex mx;
        manapi::reference<parallel_t> st;
        value_t value;
    };

    template<>
    class parallel_run<void> : public std::enable_shared_from_this<parallel_run<void>>{
    public:
        struct value_t {
        public:
            manapi::future<manapi::status> run (manapi::future<void> task) {
                try {
                    co_await task;
                    co_return manapi::status_ok();
                }
                catch (std::exception const &e) {
                    manapi_log_trace("%s due to %s", "parallel_run:Failed", e.what());
                    co_return manapi::status_internal("parallel_run:Failed");
                }
            }

            manapi::status get () { return manapi::status_ok(); }
        };

        parallel_run (manapi::reference<parallel_t> st)  {
            this->st = std::move(st);
        }

        static manapi::status_or<std::shared_ptr<parallel_run>> create (manapi::reference<parallel_t> st) MANAPIHTTP_NOEXCEPT {
            try {
                return std::make_shared<parallel_run>(std::move(st));
            }
            catch (std::exception const &) {
                return manapi::status_resource_exhausted();
            }
        }

        static manapi::status_or<std::shared_ptr<parallel_run>> create (manapi::reference<parallel_t> st, manapi::future<> task) MANAPIHTTP_NOEXCEPT {
            try {
                auto z = std::make_shared<parallel_run>(std::move(st));
                z->run(std::move(task));
                return std::move(z);
            }
            catch (std::exception const &e) {
                return manapi::status_resource_exhausted();
            }
        }

        ~parallel_run () = default;

        manapi::status run (manapi::future<> task);

        MANAPIHTTP_NODISCARD manapi::future<> get () {
            auto lk = co_await this->mx.lock_guard();
        }
    private:
        manapi::async::mutex mx;
        manapi::reference<parallel_t> st;
        value_t value;
    };

    template<typename T>
    manapi::status parallel_run<T>::run(manapi::future<T> task) {
        if (this->mx.try_to_lock()) {
            async::run<manapi::status>(this->value.run(std::move(task)),
               [data = this->shared_from_this()](std::exception_ptr err, manapi::status *status)
                       -> void {
                   data->mx.unlock();
               });
            return manapi::status_ok();
        }
        else {
            return manapi::status_unavailable("parallel_run:busy");
        }
    }

    template <typename T = void>
    manapi::future<T> parallel_wait_get ( auto &&cb ) {
        using promise = manapi::async::promise_async<void>;
        std::optional<T> res; co_await promise ([&cb, &res] ( promise::resolve_t resolve ) -> manapi::future<> {
            auto parallel_ctx = manapi::reference <parallel_t> (new parallel_t());
            parallel_ctx->resolve = std::move(resolve);
            res = co_await cb ( parallel_ctx );
        });
        co_return std::move(res.value());
    }

    manapi::future<void> parallel_wait ( auto &&cb ) {
        using promise = manapi::async::promise_async<void>;
        co_await promise ([&cb] ( promise::resolve_t resolve ) -> manapi::future<> {
            auto parallel_ctx = manapi::reference <parallel_t> (new parallel_t());
            parallel_ctx->resolve = std::move(resolve);
            co_await cb ( parallel_ctx );
        });
    }
}
