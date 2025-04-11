#include "async/ManapiAsyncMutex.hpp"

bool manapi::async::mutex::promise::await_ready() noexcept { return false; }

void manapi::async::mutex::promise::await_resume() noexcept {}

void manapi::async::mutex::promise::await_suspend(std::coroutine_handle<future<>::promise> handle) {
    std::unique_lock <std::mutex> lk (this->mx);
    if (this->own) {
        this->stack.push_back(std::exchange(handle, nullptr));
    }
    else {
        this->own = true;
        lk.unlock();
        future<>::resume_promise(handle);
    }
}

manapi::async::mutex::mutex(std::shared_ptr<manapi::threadpool<task>> taskpool_)  : taskpool(std::move(taskpool_)) {}

manapi::async::mutex::mutex(const std::shared_ptr<manapi::async::context> &ctx) : taskpool(ctx->taskpool()) {}

manapi::async::mutex::mutex(mutex &&n) noexcept {
    this->own = std::exchange(n.own, false);
    this->stack = std::move(n.stack);
    this->taskpool = n.taskpool;
}

manapi::async::mutex & manapi::async::mutex::operator=(mutex &&n) noexcept {
    this->own = std::exchange(n.own, false);
    this->stack = std::move(n.stack);
    this->taskpool = n.taskpool;
    return *this;
}

manapi::future<void> manapi::async::mutex::lock(){
    co_await async::mutex::promise {this->mx, this->stack, this->own};
    co_return;
}

bool manapi::async::mutex::try_to_lock() {
    std::lock_guard<std::mutex> lk (this->mx);
    if (this->own) { return false; }
#ifdef _WIN32
    this->own = ::GetCurrentThreadId();
#else
    this->own = true;
#endif
    return true;
}

void manapi::async::mutex::unlock()  {
    std::lock_guard<std::mutex> lk (this->mx);
    if (!this->own) {
        return;
    }
    if (this->stack.empty()) {
        this->own = false;
        return;
    }
    auto handle = this->stack.back();
    this->stack.pop_back();
    if (this->stack.empty()) {
        stack = {}; // free
    }
    this->taskpool->append_task([handle = std::exchange(handle, nullptr)] () -> void {
        future<>::resume_promise(handle);
    });
}

bool manapi::async::mutex::locked() {
    std::lock_guard<std::mutex> lk (this->mx);
    return this->own;
}

manapi::future<manapi::before_delete> manapi::async::mutex::lock_guard()  {
    co_await this->lock();
    co_return before_delete([this] () -> void {
        this->unlock();
    });
}

manapi::async::mutex::~mutex() {
    std::lock_guard<std::mutex> lk (this->mx);
    if (!this->stack.empty()) {
        std::cerr << "~async::mutex(): i can't work anymore, i'm sorrryyy :(. stack.empty() != true\n";
    }
}