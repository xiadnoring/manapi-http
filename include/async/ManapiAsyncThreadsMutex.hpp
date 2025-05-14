#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../ManapiBeforeDelete.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    class tmutex {
    public:
        tmutex ();

        manapi::future<void> lock ();

        bool try_to_lock ();

        void unlock ();

        future<sbefore_delete> lock_guard ();

        ~tmutex ();
    private:
        std::mutex mx;
        bool locked_;
        manapi::chain<manapi::ev::shared_async> waiters;
    };
}