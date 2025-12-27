#include "std/ManapiAsyncMutex.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "ManapiThreadPool.hpp"

struct mutex_promise {
    std::vector <std::coroutine_handle<>> &stack;
    bool &own;

    bool await_ready () MANAPIHTTP_NOEXCEPT;

    void await_resume () MANAPIHTTP_NOEXCEPT;

    void await_suspend (std::coroutine_handle<> handle);
};

bool mutex_promise::await_ready() MANAPIHTTP_NOEXCEPT {
    return false; }

void mutex_promise::await_resume() MANAPIHTTP_NOEXCEPT {
}

void mutex_promise::await_suspend(std::coroutine_handle<> handle) {
    if (this->own) {
        MANAPIHTTP_MUST_ALLOC_START
        this->stack.push_back(std::exchange(handle, nullptr));
        MANAPIHTTP_MUST_ALLOC_END
    }
    else {
        this->own = true;
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
    this->own = false;
}


manapi::async::mutex::mutex(mutex &&n) MANAPIHTTP_NOEXCEPT {
    this->own = std::exchange(n.own, false);
    this->stack = std::move(n.stack);
}

manapi::async::mutex & manapi::async::mutex::operator=(mutex &&n) MANAPIHTTP_NOEXCEPT {
    this->own = std::exchange(n.own, false);
    this->stack = std::move(n.stack);
    return *this;
}

manapi::future<void> manapi::async::mutex::lock() {
    co_await mutex_promise {this->stack, this->own};
    co_return;
}

bool manapi::async::mutex::try_to_lock() MANAPIHTTP_NOEXCEPT {
    if (this->own) { return false; }
    this->own = true;
    return true;
}

void manapi::async::mutex::unlock() MANAPIHTTP_NOEXCEPT {
    if (!this->own) {
        return;
    }
    if (this->stack.empty()) {
        this->own = false;
        return;
    }

    auto handle = this->stack.back();
    this->stack.pop_back();

    MANAPIHTTP_MUST_ALLOC_START
    manapi::async::current()->etaskpool()->append_static_task(
        [handle] () -> void {
        handle.resume();
    });
    MANAPIHTTP_MUST_ALLOC_END
}

manapi::future<manapi::async::mutex_locker> manapi::async::mutex::lock_guard()  {
    co_await this->lock();
    co_return async::mutex_locker{this};
}

std::size_t manapi::async::mutex::waiting() const MANAPIHTTP_NOEXCEPT {
    return this->own + this->stack.size();
}

manapi::async::mutex::~mutex() {
    /* unlock everything ! */
    if (!this->stack.empty())
        this->unlock();
}