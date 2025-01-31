#pragma once

#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    template<typename T = void>
    class parallel_run {
    public:
        struct value_t {
        public:
            manapi::future<> run (manapi::future<T> task) {
                this->value = co_await task;
                task.reset();
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

        explicit parallel_run (std::shared_ptr<context> ctx) {
            this->mx = std::make_shared<async::mutex>(ctx);
            this->ctx = std::move(ctx);
        }

        parallel_run (std::shared_ptr<context> ctx, manapi::future<T> task) {
            this->mx = std::make_shared<async::mutex>(ctx);
            this->ctx = std::move(ctx);

            this->run(std::move(task));
        }

        ~parallel_run () = default;

        void run (manapi::future<T> task);

        [[nodiscard]] manapi::future<T> get () const;
        template<typename T1 = T>
        requires(!std::is_same_v<T1, void>)
        [[nodiscard]] manapi::future<T1> get_or (T1 &&v) const;

    private:
        std::shared_ptr<value_t> value{nullptr};
        std::shared_ptr<async::mutex> mx;
        std::shared_ptr<async::context> ctx;
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
            async::run(this->ctx->taskpool(), std::move(this->value->run(std::move(task))),
            [mx = this->mx] ()
                -> void { mx->unlock(); });
        }
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
