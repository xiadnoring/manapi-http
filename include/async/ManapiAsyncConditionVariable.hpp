#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "../ManapiAsync.hpp"
#include "components/ManapiChain.hpp"
#include "./ManapiAsyncMutex.hpp"

namespace manapi::async {
    class condition_variable {
    private:
        struct notify_sub_t {
            std::coroutine_handle<future<>::promise> handle;
            std::function<bool()> cond;
            async::mutex *mx;
        };
    public:
        struct promise {
            std::function<bool()> cond;
            std::shared_ptr<async::mutex> gmx;
            async::mutex *mx;
            std::shared_ptr<threadpool<task>> &taskpool;
            chain <notify_sub_t> *stack;

            bool await_ready () noexcept { return false; }
            void await_resume () noexcept {}

            void await_suspend (std::coroutine_handle<future<>::promise> handle);
        };

        explicit condition_variable (const std::shared_ptr<async::context> &ctx);
        explicit condition_variable (const std::shared_ptr<threadpool<task>> &taskpool);

        future<void> wait (const std::function<bool()> &cond);

        future<void> wait (async::mutex &mx, const std::function<bool()> &cond);

        future<void> notify_one ();

        future<void> notify_all ();

        ~condition_variable () = default;
    private:
        future<void> _notify_item (chain<notify_sub_t>::iterator it);
        future<bool> _notify_first ();
        std::atomic<bool> stop = false;
        std::atomic<int> cnt = 0;
        std::shared_ptr<threadpool<task>> taskpool;
        std::shared_ptr<async::mutex> mx;
        chain <notify_sub_t> stack;
    };
}
