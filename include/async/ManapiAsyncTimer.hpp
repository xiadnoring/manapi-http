#pragma once

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../services/ManapiTimerPool.hpp"

namespace manapi::async {
    class delay {
    public:
        delay (size_t ms)  {
            this->time = ms;
        }
        ~delay() = default;
        [[nodiscard]] bool await_ready () const {
            return false;
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            manapi::async::current()->timerpool()->append_timer_sync(this->time, [handle] (manapi::timer t) -> void {
                future<>::resume_promise(handle);
            });
        }
        void await_resume () const {}
    private:
        size_t time{};
    };
}