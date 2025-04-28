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
        struct promise {
            std::mutex &mx;
            manapi::chain <std::coroutine_handle<future<>::promise> > &stack;
            bool &own;

            bool await_ready () noexcept;
            void await_resume () noexcept;
            void await_suspend (std::coroutine_handle<future<>::promise> handle);
        };
        mutex (std::shared_ptr<manapi::threadpool<task>> taskpool_);
        mutex (const async::shared_ctx &ctx);

        mutex (mutex &&n) noexcept;
        mutex &operator=(mutex &&n) noexcept;

        manapi::future<void> lock ();

        bool try_to_lock ();

        void unlock ();

        [[nodiscard]] bool locked ();

        future<before_delete> lock_guard ();

        ~mutex ();
    private:
        std::shared_ptr<threadpool<task>> taskpool;
        std::mutex mx;
        bool own{false};
        manapi::chain <std::coroutine_handle<future<>::promise> > stack;
    };
}