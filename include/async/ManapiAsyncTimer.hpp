#pragma once

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../services/ManapiTimerPool.hpp"

namespace manapi::async {
    class delay {
    public:
        delay (size_t ms, manapi::async::cancellation_action cancellation = nullptr)  {
            if (cancellation)
                this->cancellation = std::move(cancellation);
            else
                this->cancellation = manapi::async::cancellation_action();

            auto const tm = this->cancellation.timeout();

            if (!tm)
                this->cancellation.timeout(ms);
            else
                this->cancellation.timeout(std::min(tm, ms));
        }
        ~delay() = default;
        [[nodiscard]] bool await_ready () const {
            return false;
        }

        template <typename T1>
        requires(std::is_base_of_v<promise_base, T1>)
        void await_suspend (std::coroutine_handle<T1> handle) {
            this->cancellation.cancel_callback(
                [handle, tmp_ = this->cancellation] () mutable -> void {
                    future<>::resume_promise(handle);
                    tmp_.reset();
            });
        }
        void await_resume () const {}
    private:
        manapi::async::cancellation_action cancellation;
    };
}