#pragma once

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../ManapiTimerPool.hpp"
#include "../std/ManapiCancelToken.hpp"

namespace manapi::async {
    class delay {
    public:
        delay (std::size_t ms, manapi::ctoken cancellation = nullptr)  {
            if (cancellation) this->m_token = std::move(cancellation);
            else this->m_token = manapi::ctoken();

            this->m_token.timeout(ms);
        }
        ~delay() = default;

        MANAPIHTTP_NODISCARD bool await_ready () const {
            return false;
        }

        void await_suspend (std::coroutine_handle<> handle) {
            this->m_token.cancel_callback(
                [handle] () mutable -> void { async::coro_resume(handle); });
        }

        void await_resume () const {}
    private:
        manapi::ctoken m_token;
    };
}