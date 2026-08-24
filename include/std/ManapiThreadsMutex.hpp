#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../std/ManapiBeforeDelete.hpp"
#include "./ManapiContext.hpp"
#include "./ManapiCancellation.hpp"

namespace manapi::async {
    class tmutex {
        struct tmutex_promise;
    public:
        tmutex ();

        manapi::future<void> lock ();

        /**
         * lock with cancellation token
         * @return true if it was locked, otherwise, it returns false
         */
        manapi::future<bool> lock(ctoken cancellation);

        bool try_to_lock ();

        void unlock ();

        future<sbefore_delete> lock_guard ();

        /**
         * lock with cancellation token
         * @return sbefore_delete if it was locked, otherwise, it returns CANCELLED error
         */
        future<manapi::status_or<sbefore_delete>> lock_guard (manapi::ctoken cancellation);

        ~tmutex ();
    private:
        std::mutex m_mx;

        bool m_locked;

        std::vector <tmutex_promise*> m_waiters;
    };
}