#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "./ManapiCancellation.hpp"

namespace manapi::async {
    class mutex;

    class mutex_locker {
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

    class mutex {
        class mutex_promise;
    public:
        mutex ();

        mutex (mutex &&n) MANAPIHTTP_NOEXCEPT;

        mutex &operator=(mutex &&n) MANAPIHTTP_NOEXCEPT;

        manapi::future<void> lock ();

        /**
         * lock with cancellation token
         * @return true if it was locked, otherwise, it returns false
         */
        manapi::future<bool> lock (manapi::ctoken cancellation);

        bool try_to_lock () MANAPIHTTP_NOEXCEPT;

        void unlock () MANAPIHTTP_NOEXCEPT;

        future<mutex_locker> lock_guard ();

        /**
         * lock with cancellation token
         * @return mutex_locker if it was locked, otherwise, it returns CANCELLED error
         */
        future<manapi::status_or<mutex_locker>> lock_guard (manapi::ctoken cancellation);

        MANAPIHTTP_NODISCARD std::size_t waiting () const MANAPIHTTP_NOEXCEPT;

        ~mutex ();
    private:
        bool m_own;
        std::vector <mutex_promise*> m_stack;
    };
}