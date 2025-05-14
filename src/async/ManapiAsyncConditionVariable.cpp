#include "async/ManapiAsyncConditionVariable.hpp"

struct manapi::async::condition_variable::promise {
    std::function<bool()> cond;
    chain <notify_sub_t> *stack;

    bool await_ready () noexcept { return false; }
    void await_resume () noexcept {}

    void await_suspend (std::coroutine_handle<future<>::promise> handle);
};

void manapi::async::condition_variable::promise::await_suspend(std::coroutine_handle<future<>::promise> handle) {
    this->stack->push_back({handle, std::move(this->cond)});
}


manapi::async::condition_variable::condition_variable() = default;

manapi::future<> manapi::async::condition_variable::wait(std::function<bool()> cond) {
    if (cond()) { co_return; }
    co_await promise{std::move(cond), &this->stack};
}

void manapi::async::condition_variable::notify_one() {
    if (this->stack.empty()) {
        return;
    }

    auto row = std::move(this->stack.back());
    this->stack.pop_back();

    try {
        if (!row.cond()) {
            goto err;
        }
    }
    catch (...) {
        goto err;
    }

    row.handle.resume();
    return;

    err: this->stack.push_back(std::move(row));
}

void manapi::async::condition_variable::notify_all() {
    while (!this->stack.empty()) {
        auto row = std::move(this->stack.back());
        this->stack.pop_back();

        try {
            if (!row.cond()) {
                goto err;
            }
        }
        catch (...) {
            goto err;
        }

        row.handle.resume();
        continue;

        err: this->stack.push_back(std::move(row));
    }
}