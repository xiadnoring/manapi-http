#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../ManapiBeforeDelete.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    class mutex {
    public:
        mutex ();

        mutex (mutex &&n) noexcept;

        mutex &operator=(mutex &&n) noexcept;

        manapi::future<void> lock ();

        bool try_to_lock ();

        void unlock ();

        future<sbefore_delete> lock_guard ();

        ~mutex ();
    private:
        bool own;
        manapi::chain <std::coroutine_handle<future<>::promise> > stack;
    };
}