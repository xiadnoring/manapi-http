#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    class mutex;

    class DLLExportImport mutex_locker {
    public:
        mutex_locker (mutex *mx);

        mutex_locker (mutex_locker &&n) MANAPIHTTP_NOEXCEPT;

        mutex_locker &operator= (mutex_locker &&) MANAPIHTTP_NOEXCEPT;

        ~mutex_locker();

        void call () MANAPIHTTP_NOEXCEPT;

        void disable () MANAPIHTTP_NOEXCEPT;
    private:
        mutex *mx;
    };

    class DLLExportImport mutex {
    public:
        mutex ();

        mutex (mutex &&n) noexcept;

        mutex &operator=(mutex &&n) noexcept;

        manapi::future<void> lock ();

        bool try_to_lock () MANAPIHTTP_NOEXCEPT;

        void unlock () MANAPIHTTP_NOEXCEPT;

        future<mutex_locker> lock_guard ();

        ~mutex ();
    private:
        bool own;
        std::vector <std::coroutine_handle<future<>::promise> > stack;
    };
}