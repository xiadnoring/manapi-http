#pragma once

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../ManapiTimerPool.hpp"
#include "../std/ManapiCancellation.hpp"

namespace manapi::async {
    class delay {
    public:
        delay (size_t ms, manapi::ctoken cancellation = nullptr)  {
            if (cancellation)
                this->cancellation = std::move(cancellation);
            else
                this->cancellation = manapi::ctoken();

            auto const tm = this->cancellation.timeout();

            if (!tm)
                this->cancellation.timeout(ms);
            else
                this->cancellation.timeout(std::min(tm, ms));
        }
        ~delay() = default;

        MANAPIHTTP_NODISCARD bool await_ready () const {
            return false;
        }

        void await_suspend (std::coroutine_handle<> handle) {
            this->cancellation.cancel_callback(
                [handle, tmp_ = this->cancellation] () mutable -> void {
                    async::coro_resume(handle);
                    tmp_.reset();
            });
        }

        void await_resume () const {}
    private:
        manapi::ctoken cancellation;
    };
}