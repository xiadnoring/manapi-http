#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiAsyncMutex.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    template<typename T = void>
    class parallel_run {
    public:
        struct value_t {
        public:
            manapi::future<manapi::error::status> run (manapi::future<T> task) {
                try {
                    if (this->flags)
                        reinterpret_cast<T *> (this->value)->~T();

                    new (this->value) T (co_await task);
                    this->flags = 1;

                    co_return manapi::error::status_ok();
                }
                catch (std::exception const &e) {
                    manapi_log_trace("%s due to %s", "parallel_run:Failed", e.what());
                    co_return manapi::error::status_internal("parallel_run:Failed");
                }
            }

            MANAPIHTTP_NODISCARD bool some () const MANAPIHTTP_NOEXCEPT {
                return this->flags;
            }

            manapi::error::status_or<T> get () {
                if (!this->flags)
                    return manapi::error::status_not_found("parallel_run:no data");
                auto v1 = std::move(*reinterpret_cast<T *> (this->value));
                if (this->flags)
                    reinterpret_cast<T *> (this->value)->~T();
                this->flags = 0;
                return std::move(v1);
            }

            T get_or (T v) {
                if (this->some())
                    return this->get().unwrap();

                return std::move(v);
            }

            ~value_t() {
                if (this->flags)
                    reinterpret_cast<T *> (this->value)->~T();
            }
        private:
            bool flags;
            uint8_t value[sizeof(T)];
        };

        struct data_t {
            manapi::async::mutex mx;
            value_t value;
        };

        parallel_run () : data(nullptr) {}

        static manapi::error::status_or<parallel_run> create () MANAPIHTTP_NOEXCEPT {
            try {
                parallel_run run;
                run.data = std::make_shared<data_t>();
                return std::move(run);
            }
            catch (std::exception const &e) {
                return manapi::error::status_resource_exhausted();
            }
        }

        static manapi::error::status_or<parallel_run> create (manapi::future<T> task) MANAPIHTTP_NOEXCEPT {
            try {
                parallel_run run;
                run.data = std::make_shared<data_t>();
                run.run(std::move(task));
                return std::move(run);
            }
            catch (std::exception const &e) {
                return manapi::error::status_resource_exhausted();
            }
        }

        ~parallel_run () = default;

        manapi::error::status run (manapi::future<T> task);
        manapi::future<manapi::error::status> async_run (manapi::future<T> task);

        template<typename T1 = T>
        requires(!std::is_same_v<T1, void>)
        MANAPIHTTP_NODISCARD manapi::future<manapi::error::status_or<T>> get () const;

        template<typename T1 = T>
        requires(std::is_same_v<T1, void>)
        MANAPIHTTP_NODISCARD manapi::future<> get () const;

        template<typename T1 = T>
        requires(!std::is_same_v<T1, void>)
        MANAPIHTTP_NODISCARD manapi::future<T1> get_or (T1 v) const;

        MANAPIHTTP_NODISCARD bool some () const MANAPIHTTP_NOEXCEPT;
    private:
        std::shared_ptr<data_t> data;
    };

    template<>
    struct parallel_run<void>::value_t {
    public:
        manapi::future<manapi::error::status> run (manapi::future<void> task) {
            try {
                co_await task;
                co_return manapi::error::status_ok();
            }
            catch (std::exception const &e) {
                manapi_log_trace("%s due to %s", "parallel_run:Failed", e.what());
                co_return manapi::error::status_internal("parallel_run:Failed");
            }
        }

        manapi::error::status get () { return manapi::error::status_ok(); }
    };

    template<typename T>
    manapi::error::status parallel_run<T>::run(manapi::future<T> task) {
        if (this->data->mx.try_to_lock()) {
            auto taskrun = this->data->value.run(std::move(task));
            async::run<manapi::error::status>(std::move(taskrun),
            [data = this->data] (std::exception_ptr err, manapi::error::status *status)
                -> void {
                data->mx.unlock();
            });
            return manapi::error::status_ok();
        }
        else {
            return manapi::error::status_unavailable("parallel_run:busy");
        }
    }

    template<typename T>
    manapi::future<manapi::error::status> parallel_run<T>::async_run(manapi::future<T> task) {
        co_await this->data->mx.lock_guard();
        auto taskrun = this->data->value.run(std::move(task));
        async::run<manapi::error::status>(std::move(taskrun),
        [data = this->data] (std::exception_ptr err, manapi::error::status *status)
            -> void {
            data->mx.unlock();
        });
    }

    // template<typename T>
    // manapi::future<void> parallel_run<T>::async_run_with_prepare(std::move_only_function<manapi::future<T>()> task, std::move_only_function<void()> cb) {
    //     co_await this->mx->lock_guard();
    //     cb();
    //     this->value = std::make_shared<value_t>();
    //     auto taskrun = this->value->run(invoke(std::move(task)));
    //     async::run(std::move(taskrun),
    //     [mx = this->mx, value = this->value] (std::exception_ptr err)
    //         -> void {
    //         mx->unlock();
    //     });
    // }

    template<typename T>
    template<typename T1>
    requires(std::is_same_v<T1, void>)
    manapi::future<> parallel_run<T>::get() const {
        auto lk = co_await this->data->mx.lock_guard();
    }


    template<typename T>
    template<typename T1>
    requires(!std::is_same_v<T1, void>)
    manapi::future<manapi::error::status_or<T>> parallel_run<T>::get() const {
        auto lk = co_await this->data->mx.lock_guard();
        co_return this->data->value.get();
    }

    template<typename T>
    template<typename T1>
    requires(!std::is_same_v<T1, void>)
    manapi::future<T1> parallel_run<T>::get_or(T1 v) const {
        auto lk = co_await this->data->mx.lock_guard();
        co_return this->data->value.get_or(std::move(v));
    }

    template<typename T>
    bool parallel_run<T>::some() const MANAPIHTTP_NOEXCEPT {
        return this->data->value.some();
    }
}
