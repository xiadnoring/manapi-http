#include "async/ManapiAsyncConditionVariable.hpp"

void manapi::async::condition_variable::promise::await_suspend(std::coroutine_handle<future<>::promise> handle) {
    async::run(this->gmx->lock(),
        [cond = std::move(this->cond), stack = this->stack, gmx = this->gmx, handle = std::exchange(handle, nullptr)]
        (std::exception_ptr err) mutable
            -> void {
        stack->push_back({handle, std::move(cond)});
        gmx->unlock();
    });
}


manapi::async::condition_variable::condition_variable() {
    this->mx = std::make_shared<async::mutex>();
}

manapi::future<> manapi::async::condition_variable::wait(std::function<bool()> cond) {
    if (cond()) { co_return; }
    co_await promise{std::move(cond), this->mx, &this->stack};
}

manapi::future<> manapi::async::condition_variable::notify_one() {
    auto lk = co_await this->mx->lock_guard();
    if (this->stack.empty()) {
        co_return;
    }
    auto &row = this->stack.back();
    if (!row.cond()) {
        co_return;
    }

    auto handle = std::move(row.handle);
    this->stack.pop_back();

    handle.resume();
}

manapi::future<> manapi::async::condition_variable::notify_all() {
    auto lk = co_await this->mx->lock_guard();
    for (auto it = this->stack.begin(); it != this->stack.end(); ) {
        if (!it->cond()) {
            it++;
            continue;
        }

        auto handle = std::move(it->handle);
        it = this->stack.erase(it);

        handle.resume();
    }
}