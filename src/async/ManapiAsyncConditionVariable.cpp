#include "async/ManapiAsyncConditionVariable.hpp"

void manapi::async::condition_variable::promise::await_suspend(std::coroutine_handle<future<>::promise> handle) {
    async::run(this->taskpool, this->gmx->lock(), [cond = std::move(this->cond), mx = this->mx, stack = this->stack, gmx = this->gmx, handle = std::exchange(handle, nullptr)] () -> void {
        stack->push({handle, std::move(cond), mx});
        gmx->unlock();
    });
}

manapi::async::condition_variable::condition_variable(const std::shared_ptr<async::context> &ctx) : taskpool(ctx->taskpool()) {
    this->mx = std::make_shared<async::mutex>(ctx);
}

manapi::async::condition_variable::condition_variable(const std::shared_ptr<threadpool<task>> &taskpool) : taskpool(taskpool) {
    this->mx = std::make_shared<async::mutex>(taskpool);
}

manapi::future<> manapi::async::condition_variable::wait(const std::function<bool()> &cond) {
    if (cond()) { co_return; }
    co_await promise{cond, this->mx, nullptr, this->taskpool, &this->stack};
}

manapi::future<> manapi::async::condition_variable::wait(async::mutex &mx, const std::function<bool()> &cond) {
    if (cond()) {
        if (!mx.locked()) {
            co_await mx.lock();
        }
        co_return;
    }
    mx.unlock();
    co_await promise{cond, this->mx, &mx, this->taskpool, &this->stack};
}

manapi::future<> manapi::async::condition_variable::notify_one() {
    auto lk = co_await this->mx->lock_guard();
    co_await this->_notify_first();
}

manapi::future<> manapi::async::condition_variable::notify_all() {
    auto lk = co_await this->mx->lock_guard();
    size_t _len = this->stack.size();
    for (size_t i = 0; i < _len; i++) {
        if (!co_await this->_notify_first()) {
            break;
        }
    }
}

manapi::future<> manapi::async::condition_variable::_notify_item(chain<notify_sub_t>::iterator it) {
    auto &data = *it;
    // if (data.mx) {
    //     //MANAPIHTTP_LOG2("--WANT 2 BLOCK");
    //     co_await data.mx->lock();
    //     //MANAPIHTTP_LOG2("--BLOCKED");
    // }

    bool rhs = false;
    try { rhs = data.cond (); }
    catch (std::exception const &e) { std::cerr << e.what() << "\n"; }

    if (rhs) {
        auto handle = std::exchange(data.handle, nullptr);
        {
            auto lk = co_await this->mx->lock_guard();
            this->stack.erase(it);
        }
        //MANAPIHTTP_LOG2("--pop_front");
        this->taskpool->append_task([handle = std::exchange(handle, nullptr)] () -> void {
            future<>::resume_promise(handle);
        });
    }
    else {
        //MANAPIHTTP_LOG2("--UNBLOCKED");
        if (data.mx) { data.mx->unlock(); }

        if (this->stop) {
            auto lk = co_await this->mx->lock_guard();
            this->stack.erase(it);
        }
    }
}

manapi::future<bool> manapi::async::condition_variable::_notify_first() {
    if (this->stack.empty()) { co_return false; }
    auto &data = *this->stack.rbegin();
    if (data.mx) { co_await data.mx->lock(); }
    async::run(this->taskpool, this->_notify_item(this->stack.rbegin()));
    co_return true;
}
