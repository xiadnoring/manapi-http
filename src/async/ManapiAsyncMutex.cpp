#include "async/ManapiAsyncMutex.hpp"

bool manapi::async::mutex::promise::await_ready() noexcept { return false; }

void manapi::async::mutex::promise::await_resume() noexcept {}

void manapi::async::mutex::promise::await_suspend(std::coroutine_handle<future<>::promise> handle) {
    std::unique_lock <std::mutex> lk (this->mx);
    if (this->own.has_value()) {
        this->stack.push(std::exchange(handle, nullptr));
    }
    else {
        this->own = std::this_thread::get_id();
        lk.unlock();
        future<>::resume_promise(handle);
    }
}

manapi::async::mutex::mutex(std::shared_ptr<manapi::threadpool<task>> taskpool_)  : taskpool(std::move(taskpool_)) {}

manapi::async::mutex::mutex(const std::shared_ptr<manapi::async::context> &ctx) : taskpool(ctx->taskpool()) {}

manapi::future<void> manapi::async::mutex::lock(){
    co_await async::mutex::promise {this->mx, this->stack, this->own};
    co_return;
}

bool manapi::async::mutex::try_to_lock() {
    std::lock_guard<std::mutex> lk (this->mx);
    if (this->own.has_value()) { return false; }
    this->own = std::this_thread::get_id();
    return true;
}

void manapi::async::mutex::unlock()  {
    std::lock_guard<std::mutex> lk (this->mx);
    if (!this->own.has_value()) {
        return;
    }
    if (this->stack.empty()) {
        this->own.reset();
        return;
    }
    auto handle = this->stack.front();
    this->stack.pop();
    if (this->stack.empty()) {
        stack = {}; // free
    }
    this->taskpool->append_task([handle = std::exchange(handle, nullptr)] () -> void {
        future<>::resume_promise(handle);
    });
}

bool manapi::async::mutex::locked() {
    std::lock_guard<std::mutex> lk (this->mx);
    return this->own.has_value();
}

manapi::future<manapi::before_delete> manapi::async::mutex::lock_guard()  {
    co_await this->lock();
    co_return before_delete([this] () -> void {
        this->unlock();
    });
}

manapi::async::mutex::~mutex() {
    std::lock_guard<std::mutex> lk (this->mx);
    if (!stack.empty()) {
        std::cerr << "~async::mutex(): i can't work anymore, i'm sorrryyy :(. stack.empty() != true\n";
    }
}