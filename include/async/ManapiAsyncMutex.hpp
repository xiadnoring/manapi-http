#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "../ManapiAsync.hpp"
#include "../ManapiBeforeDelete.hpp"
#include "./ManapiAsyncContext.hpp"

namespace manapi::async {
    class mutex {
    public:
        struct promise {
            std::mutex &mx;
            std::queue <std::coroutine_handle<future<>::promise> > &stack;
            std::optional<std::thread::id> &own;

            bool await_ready () noexcept;
            void await_resume () noexcept;
            void await_suspend (std::coroutine_handle<future<>::promise> handle);
        };

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
        std::optional<std::thread::id> own;
        std::queue <std::coroutine_handle<future<>::promise> > stack;
    };
}