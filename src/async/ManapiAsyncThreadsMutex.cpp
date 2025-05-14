#include "async/ManapiAsyncThreadsMutex.hpp"

struct tmutex_promise {
    manapi::chain <manapi::ev::shared_async> &waiters;
    bool &locked_;
    std::mutex &mx;

    bool await_ready () noexcept;
    void await_resume () noexcept;
    void await_suspend (std::coroutine_handle<manapi::future<>::promise> handle);
};

bool tmutex_promise::await_ready() noexcept { return false; }

void tmutex_promise::await_resume() noexcept {}

void tmutex_promise::await_suspend(std::coroutine_handle<manapi::future<>::promise> handle) {
    std::unique_lock<std::mutex> lk (this->mx);

    if (this->locked_) {
        auto watcher = manapi::async::current()->eventloop()->create_watcher_async([handle] (manapi::ev::shared_async &w) -> void {
            auto handle_ = handle;
            manapi::async::current()->eventloop()->stop_watcher(w);

            manapi::future<>::resume_promise(handle_);
        });

        this->waiters.push_back(std::move(watcher));
    }
    else {
        this->locked_ = true;
        lk.unlock();
        manapi::future<>::resume_promise(handle);
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

    auto handle = std::move(this->waiters.back());
    this->waiters.pop_back();

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