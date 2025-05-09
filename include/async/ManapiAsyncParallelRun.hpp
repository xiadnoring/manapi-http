#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    template<typename T = void>
    class parallel_run {
    public:
        struct value_t {
        public:
            manapi::future<> run (manapi::future<T> task) {
                this->value = co_await task;
            }

            T get () {
                auto v1 = std::move(this->value.value());
                this->value.reset();
                return std::move(v1);
            }

            T get_or (T &&v) {
                auto v1 = std::move(this->value.value_or(std::forward<decltype(v)>(v)));
                this->value.reset();
                return std::move(v1);
            }
        private:
            std::optional<T> value{};
        };

        parallel_run (shared_cthread ctx) {
            this->mx = std::make_shared<async::mutex>(ctx);
            this->ctx = std::move(ctx);
        }

        parallel_run (shared_cthread ctx, manapi::future<T> task) {
            this->mx = std::make_shared<async::mutex>(ctx);
            this->ctx = std::move(ctx);

            this->run(std::move(task));
        }

        ~parallel_run () = default;

        void run (manapi::future<T> task);
        manapi::future<> async_run (manapi::future<T> task);

        [[nodiscard]] manapi::future<T> get () const;
        template<typename T1 = T>
        requires(!std::is_same_v<T1, void>)
        [[nodiscard]] manapi::future<T1> get_or (T1 &&v) const;
        manapi::future<void> async_run_with_prepare(std::move_only_function<manapi::future<T>()> task, std::move_only_function<void()> cb);
    private:
        std::shared_ptr<value_t> value{nullptr};
        std::shared_ptr<async::mutex> mx;
        async::shared_cthread ctx;
    };

    template<>
    struct parallel_run<void>::value_t {
    public:
        manapi::future<> run (manapi::future<void> task) {
            co_await task;
        }

        void get () {}
    };

    template<typename T>
    void parallel_run<T>::run(manapi::future<T> task) {
        if (this->mx->try_to_lock()) {
            this->value = std::make_shared<value_t>();
            auto taskrun = this->value->run(std::move(task));
            async::run(this->ctx->etaskpool(), std::move(taskrun),
            [mx = this->mx, value = this->value] (std::exception_ptr err)
                -> void {
                mx->unlock();
            });
        }
        else {
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_SUBSCRIBE_FAILURE, "parallel run is busy");
        }
    }

    template<typename T>
    manapi::future<void> parallel_run<T>::async_run(manapi::future<T> task) {
        co_await this->mx->lock_guard();
        this->value = std::make_shared<value_t>();
        auto taskrun = this->value->run(std::move(task));
        async::run(this->ctx->etaskpool(), std::move(taskrun),
        [mx = this->mx, value = this->value] (std::exception_ptr err)
            -> void {
            mx->unlock();
        });
    }

    template<typename T>
    manapi::future<void> parallel_run<T>::async_run_with_prepare(std::move_only_function<manapi::future<T>()> task, std::move_only_function<void()> cb) {
        co_await this->mx->lock_guard();
        cb();
        this->value = std::make_shared<value_t>();
        auto taskrun = this->value->run(invoke(std::move(task)));
        async::run(this->ctx->etaskpool(), std::move(taskrun),
        [mx = this->mx, value = this->value] ()
            -> void {
            mx->unlock();
        });
    }

    template<typename T>
    manapi::future<T> parallel_run<T>::get() const {
        auto lk = co_await this->mx->lock_guard();
        co_return this->value->get();
    }

    template<typename T>
    template<typename T1>
    requires(!std::is_same_v<T1, void>)
    manapi::future<T1> parallel_run<T>::get_or(T1 &&v) const {
        auto lk = co_await this->mx->lock_guard();
        if (this->value) {
            co_return this->value->get_or(std::forward<decltype(v)>(v));
        }
        co_return std::forward<decltype(v)>(v);
    }
}
