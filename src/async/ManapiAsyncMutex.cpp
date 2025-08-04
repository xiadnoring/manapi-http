#include "async/ManapiAsyncMutex.hpp"

struct mutex_promise {
    std::vector <std::coroutine_handle<manapi::future<>::promise>> stack;
    bool &own;

    bool await_ready () noexcept;
    void await_resume () noexcept;
    void await_suspend (std::coroutine_handle<manapi::future<>::promise> handle);
};

bool mutex_promise::await_ready() noexcept { return false; }

void mutex_promise::await_resume() noexcept {}

void mutex_promise::await_suspend(std::coroutine_handle<manapi::future<>::promise> handle) {
    if (this->own) {
        MANAPIHTTP_MUST_ALLOC_START
        this->stack.push_back(std::exchange(handle, nullptr));
        MANAPIHTTP_MUST_ALLOC_END
    }
    else {
        this->own = true;
        manapi::future<>::resume_promise(handle);
    }
}

manapi::async::mutex::mutex() {
    this->own = false;
}


manapi::async::mutex::mutex(mutex &&n) noexcept {
    this->own = std::exchange(n.own, false);
    this->stack = std::move(n.stack);
}

manapi::async::mutex & manapi::async::mutex::operator=(mutex &&n) noexcept {
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

    manapi::async::current()->etaskpool()->append_task(
        [handle = std::exchange(handle, nullptr)] () -> void {
        future<>::resume_promise(handle);
    });
}

manapi::future<manapi::sbefore_delete> manapi::async::mutex::lock_guard()  {
    co_await this->lock();
    co_return sbefore_delete([this] () -> void {
        this->unlock();
    });
}

manapi::async::mutex::~mutex() {
    /* unlock everything ! */
    if (!this->stack.empty())
        this->unlock();
}