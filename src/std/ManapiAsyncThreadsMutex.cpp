#include "ManapiEventLoop.hpp"
#include "std/ManapiAsyncThreadsMutex.hpp"

struct manapi::async::tmutex::tmutex_promise {
    tmutex *parent;
    manapi::ctoken *token;
    std::size_t pos;
    ev::shared_async w;
    std::coroutine_handle<> handle;

    bool await_ready () MANAPIHTTP_NOEXCEPT;

    void await_resume () MANAPIHTTP_NOEXCEPT;

    void await_suspend (std::coroutine_handle<> handle);
};

bool manapi::async::tmutex::tmutex_promise::await_ready() MANAPIHTTP_NOEXCEPT {
    return false;
}

void manapi::async::tmutex::tmutex_promise::await_resume() MANAPIHTTP_NOEXCEPT {
    if (this->token) {
        this->token->disable();
    }
}

void manapi::async::tmutex::tmutex_promise::await_suspend(std::coroutine_handle<> handle) {
    std::unique_lock<std::mutex> lk (this->parent->m_mx);

    if (this->parent->m_locked) {
        this->handle = handle;

        if (this->token) {
            this->token->cancel_callback([this] () -> void {
                std::unique_lock<std::mutex> lk (this->parent->m_mx);

                if (this->pos == std::numeric_limits<std::size_t>::max()) {
                    return;
                }

                assert(!this->parent->m_waiters.empty());
                std::swap(this->parent->m_waiters[this->pos], this->parent->m_waiters.back());
                std::swap(this->parent->m_waiters[this->pos]->pos, this->parent->m_waiters.back()->pos);

                this->parent->m_waiters.pop_back();
                manapi::async::current()->eventloop()->stop_watcher(this->w);

                lk.unlock();

                async::coro_resume(this->handle);
            });
        }

        this->w = manapi::async::eventloop()->create_watcher_async(
            [z = this] (const manapi::ev::shared_async &w) -> void {
            auto p = z;
            assert(p->w == w);
            manapi::async::current()->eventloop()->stop_watcher(p->w);
            if (p->token)
                p->token->disable();
            async::coro_resume(p->handle);
        }).unwrap();

        this->pos = this->parent->m_waiters.size();
        this->parent->m_waiters.push_back(this);
    }
    else {
        this->parent->m_locked = true;
        lk.unlock();

        async::coro_resume(handle);
    }
}

manapi::async::tmutex::tmutex() {
    this->m_locked = false;
}

manapi::future<void> manapi::async::tmutex::lock(){
    co_await tmutex_promise {this, nullptr};
    co_return;
}

manapi::future<bool> manapi::async::tmutex::lock(ctoken cancellation) {
    co_await tmutex_promise {this, &cancellation};
    co_return !cancellation.is_cancelled();
}

bool manapi::async::tmutex::try_to_lock() {
    std::lock_guard<std::mutex> lk (this->m_mx);
    if (this->m_locked) { return false; }
    this->m_locked = true;
    return true;
}

void manapi::async::tmutex::unlock()  {
    std::lock_guard<std::mutex> lk (this->m_mx);

    if (!this->m_locked) {
        return;
    }

    if (this->m_waiters.empty()) {
        this->m_locked = false;
        return;
    }

    auto handle = this->m_waiters.back();
    this->m_waiters.pop_back();
    handle->pos = std::numeric_limits<std::size_t>::max();
    if (auto rhs = handle->w->send())
        manapi_log_ferror("%s failed due to %s", "send()", ev::strerror(rhs));
}

manapi::future<manapi::sbefore_delete> manapi::async::tmutex::lock_guard()  {
    co_await this->lock();
    co_return sbefore_delete([this] () -> void {
        this->unlock();
    });
}

manapi::future<manapi::status_or<manapi::sbefore_delete>> manapi::async::tmutex::lock_guard(manapi::ctoken cancellation) {
    if (co_await this->lock(std::move(cancellation))) {
        co_return sbefore_delete([this] () -> void {
            this->unlock();
        });
    }
    co_return manapi::status_cancelled("tmutex:lock_guard cancelled");
}

manapi::async::tmutex::~tmutex() {
    /* unlock everything ! */
    if (!this->m_waiters.empty()) {
        this->unlock();
    }
}
