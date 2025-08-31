#pragma once

#include <atomic>
#include <queue>
#include <utility>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../std/ManapiChain.hpp"
#include "./ManapiAsyncMutex.hpp"

namespace manapi::async {
    class condition_variable {
    private:
        struct notify_sub_t {
            std::coroutine_handle<future<>::promise> handle;
            std::function<bool()> cond;
        };
    public:
        struct promise;

        condition_variable ();

        future<void> wait (std::function<bool()> cond);

        void notify_one ();

        void notify_all ();

        ~condition_variable () = default;
    private:
        bool stop = false;
        chain <notify_sub_t> stack;
    };
}
