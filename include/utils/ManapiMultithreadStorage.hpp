#pragma once

#include <mutex>
#include <vector>
#include <memory>
#include <functional>

#include "../std/ManapiAsyncContext.hpp"
#include "../std/ManapiAsyncThreadsMutex.hpp"
#include "../std/ManapiBeforeDelete.hpp"

namespace manapi {
    class multithread_storage {
        struct data_t;
    public:
        typedef std::move_only_function<void(const manapi::json &n)> subscribe_cb;

        class worker_t {
        public:
            worker_t (std::shared_ptr<void> data);

            void *pointer ();

            template<typename T>
            T *as () { return static_cast<T *>(this->pointer()); }

            subscribe_cb cb;
            ev::shared_async w;
        private:
            std::shared_ptr<void> data;
        };

        multithread_storage (void *ptr, std::function<void(void *n)> deleter);

        multithread_storage (manapi::json n, void *ptr, std::function<void(void *n)> deleter);

        ~multithread_storage ();

        multithread_storage (const multithread_storage &n);

        multithread_storage &operator=(const multithread_storage &n);

        manapi::future<std::shared_ptr<worker_t>> subscribe (subscribe_cb cb);

        manapi::future<manapi::error::status> edit (const std::shared_ptr<worker_t> &m, std::move_only_function<bool(manapi::json &data)> cb);

        manapi::future<manapi::error::status> edit_async (const std::shared_ptr<worker_t> &m, std::move_only_function<manapi::future<bool>(manapi::json &data)> cb);

        manapi::future<> unsubscribe (const std::shared_ptr<worker_t> &m);

        manapi::future<sbefore_delete> write_lock ();

        void *pointer () MANAPIHTTP_NOEXCEPT;

        template<typename T>
        T *as () { return static_cast<T *>(this->pointer()); }
    private:
        void notify_ (const std::shared_ptr<worker_t> &m) MANAPIHTTP_NOEXCEPT;

        void unsubscribe_ (const std::shared_ptr<worker_t> &w) MANAPIHTTP_NOEXCEPT;

        manapi::error::status call_sync_callback_ (worker_t *w) MANAPIHTTP_NOEXCEPT;

        void call_callback_ (worker_t *w) MANAPIHTTP_NOEXCEPT;

        std::shared_ptr<data_t> data_;
    };
}
