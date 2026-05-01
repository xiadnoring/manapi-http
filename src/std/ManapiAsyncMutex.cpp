#include "std/ManapiAsyncMutex.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "ManapiThreadPool.hpp"

struct manapi::async::mutex::mutex_promise {
    mutex *parent;
    manapi::ctoken *cancellation;
    std::coroutine_handle<> handle;
    std::size_t pos;

    bool await_ready () MANAPIHTTP_NOEXCEPT;

    void await_resume () MANAPIHTTP_NOEXCEPT;

    void await_suspend (std::coroutine_handle<> handle);
};

bool manapi::async::mutex::mutex_promise::await_ready() MANAPIHTTP_NOEXCEPT {
    return false; }

void manapi::async::mutex::mutex_promise::await_resume() MANAPIHTTP_NOEXCEPT {
    if (this->cancellation) {
        this->cancellation->disable();
    }
}

void manapi::async::mutex::mutex_promise::await_suspend(std::coroutine_handle<> handle) {
    if (this->parent->m_own) {
        this->handle = handle;
        this->pos = this->parent->m_stack.size();
        try {
            if (this->cancellation) {
                this->cancellation->cancel_callback(
                    [this] () -> void {
                    assert(!this->parent->m_stack.empty());
                    std::swap(this->parent->m_stack[this->pos], this->parent->m_stack.back());
                    std::swap(this->parent->m_stack[this->pos]->pos, this->parent->m_stack.back()->pos);

                    this->parent->unlock();
                });
            }

            this->parent->m_stack.push_back(this);
        }
        catch (...) {
            this->await_resume();
            std::rethrow_exception(std::current_exception());
        }
    }
    else {
        this->parent->m_own = true;
        handle.resume();
    }
}

manapi::async::mutex_locker::mutex_locker(mutex *mx) : mx(mx) {
}

manapi::async::mutex_locker::mutex_locker(mutex_locker &&n) MANAPIHTTP_NOEXCEPT {
    this->mx = n.mx;
    n.mx = nullptr;
}

manapi::async::mutex_locker & manapi::async::mutex_locker::operator=(mutex_locker &&n) MANAPIHTTP_NOEXCEPT {
    this->mx = n.mx;
    n.mx = nullptr;
    return *this;
}

manapi::async::mutex_locker::~mutex_locker() {
    this->call();
}

void manapi::async::mutex_locker::call() MANAPIHTTP_NOEXCEPT {
    if (this->mx) {
        this->mx->unlock();
    }
}

void manapi::async::mutex_locker::disable() MANAPIHTTP_NOEXCEPT {
    this->mx = nullptr;
}

manapi::async::mutex::mutex() {
    this->m_own = false;
}


manapi::async::mutex::mutex(mutex &&n) MANAPIHTTP_NOEXCEPT {
    this->m_own = std::exchange(n.m_own, false);
    this->m_stack = std::move(n.m_stack);
}

manapi::async::mutex & manapi::async::mutex::operator=(mutex &&n) MANAPIHTTP_NOEXCEPT {
    this->m_own = std::exchange(n.m_own, false);
    this->m_stack = std::move(n.m_stack);
    return *this;
}

manapi::future<void> manapi::async::mutex::lock() {
    co_await mutex_promise {this, nullptr};
    co_return;
}

manapi::future<bool> manapi::async::mutex::lock(manapi::ctoken cancellation) {
    co_await mutex_promise {this, &cancellation};
    co_return !cancellation.is_cancelled();
}

bool manapi::async::mutex::try_to_lock() MANAPIHTTP_NOEXCEPT {
    if (this->m_own) { return false; }
    this->m_own = true;
    return true;
}

void manapi::async::mutex::unlock() MANAPIHTTP_NOEXCEPT {
    if (!this->m_own) {
        return;
    }
    if (this->m_stack.empty()) {
        this->m_own = false;
        return;
    }

    auto promise = this->m_stack.back();
    this->m_stack.pop_back();

    MANAPIHTTP_MUST_ALLOC_START
    manapi::async::current()->etaskpool()->append_task(
        [handle = promise->handle] () -> void {
        handle.resume();
    });
    MANAPIHTTP_MUST_ALLOC_END
}

manapi::future<manapi::async::mutex_locker> manapi::async::mutex::lock_guard()  {
    co_await this->lock();
    co_return async::mutex_locker{this};
}

manapi::future<manapi::status_or<manapi::async::mutex_locker>> manapi::async::mutex::lock_guard(manapi::ctoken cancellation) {
    if (co_await this->lock(std::move(cancellation)))
        co_return async::mutex_locker{this};
    co_return manapi::status_cancelled("mutex:lock_guard cancelled");
}

std::size_t manapi::async::mutex::waiting() const MANAPIHTTP_NOEXCEPT {
    return this->m_own + this->m_stack.size();
}

manapi::async::mutex::~mutex() {
    /* unlock everything ! */
    if (!this->m_stack.empty())
        this->unlock();
}