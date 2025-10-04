#include "ManapiEventLoop.hpp"
#include "std/ManapiAsyncThreadsMutex.hpp"

struct tmutex_promise {
    manapi::chain <manapi::ev::shared_async> &waiters;
    bool &locked_;
    std::mutex &mx;

    bool await_ready () MANAPIHTTP_NOEXCEPT;
    void await_resume () MANAPIHTTP_NOEXCEPT;
    void await_suspend (std::coroutine_handle<> handle);
};

bool tmutex_promise::await_ready() MANAPIHTTP_NOEXCEPT {
    return false;
}

void tmutex_promise::await_resume() MANAPIHTTP_NOEXCEPT {}

void tmutex_promise::await_suspend(std::coroutine_handle<> handle) {
    std::unique_lock<std::mutex> lk (this->mx);

    if (this->locked_) {
        auto watcher = manapi::async::current()->eventloop()->create_watcher_async([handle] (const manapi::ev::shared_async &w) -> void {
            auto handle_ = handle;
            manapi::async::current()->eventloop()->stop_watcher(w);

            handle_.resume();
        });

        this->waiters.push_back(std::move(watcher.unwrap()));
    }
    else {
        this->locked_ = true;
        lk.unlock();
        handle.resume();
    }
}

manapi::async::tmutex::tmutex() {
    this->locked_ = false;
}

manapi::future<void> manapi::async::tmutex::lock(){
    co_await tmutex_promise {this->waiters, this->locked_, this->mx};
    co_return;
}

bool manapi::async::tmutex::try_to_lock() {
    std::lock_guard<std::mutex> lk (this->mx);
    if (this->locked_) { return false; }
    this->locked_ = true;
    return true;
}

void manapi::async::tmutex::unlock()  {
    std::lock_guard<std::mutex> lk (this->mx);

    if (!this->locked_) {
        return;
    }

    if (this->waiters.empty()) {
        this->locked_ = false;
        return;
    }

    auto handle = std::move(this->waiters.front());
    this->waiters.pop_front();

    handle->send();
}

manapi::future<manapi::sbefore_delete> manapi::async::tmutex::lock_guard()  {
    co_await this->lock();
    co_return sbefore_delete([this] () -> void {
        this->unlock();
    });
}

manapi::async::tmutex::~tmutex() {
    /* unlock everything ! */
    if (!this->waiters.empty()) {
        this->unlock();
    }
}