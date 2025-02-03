#pragma once

#include <atomic>
#include <queue>
#include <utility>

#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <processthreadsapi.h>
#endif

#include "../ManapiAsync.hpp"
#include "../ManapiBeforeDelete.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    class mutex {
    public:
#ifdef _WIN32
        struct promise {
            std::mutex &mx;
            manapi::chain <std::coroutine_handle<future<>::promise> > &stack;
            std::optional<DWORD> &own;

            bool await_ready () noexcept;
            void await_resume () noexcept;
            void await_suspend (std::coroutine_handle<future<>::promise> handle);
        };
#else
        struct promise {
            std::mutex &mx;
            manapi::chain <std::coroutine_handle<future<>::promise> > &stack;
            std::optional<std::thread::id> &own;

            bool await_ready () noexcept;
            void await_resume () noexcept;
            void await_suspend (std::coroutine_handle<future<>::promise> handle);
        };
#endif
        explicit mutex (std::shared_ptr<manapi::threadpool<task>> taskpool_);
        mutex (const std::shared_ptr<manapi::async::context> &ctx);

        manapi::future<void> lock ();

        bool try_to_lock ();

        void unlock ();

        [[nodiscard]] bool locked ();

        future<before_delete> lock_guard ();

        ~mutex ();
    private:
        std::shared_ptr<threadpool<task>> taskpool;
        std::mutex mx;
#ifdef _WIN32
        std::optional<DWORD> own;
#else
        std::optional<std::thread::id> own;
#endif
        manapi::chain <std::coroutine_handle<future<>::promise> > stack;
    };
}