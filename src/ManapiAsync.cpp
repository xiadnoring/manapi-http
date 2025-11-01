#include "ManapiAsync.hpp"
#include "ManapiThreadPool.hpp"
#include "std/ManapiAsyncContext.hpp"

static size_t max_stack_depth_ = 300;
thread_local std::size_t current_stack_cnt = 0;
thread_local std::shared_ptr<manapi::async::cthread> current_cthread_ = nullptr;

void manapi::async::internal::current_(std::shared_ptr<cthread> ctx) MANAPIHTTP_NOEXCEPT {
    current_cthread_ = std::move(ctx);
}

const std::shared_ptr<manapi::async::cthread> & manapi::async::internal::current_() MANAPIHTTP_NOEXCEPT {
    return current_cthread_;
}

std::size_t manapi::async::internal::current_stack_cnt_crt () MANAPIHTTP_NOEXCEPT {
    return current_stack_cnt;
}

void manapi::async::internal::current_stack_cnt_set (std::size_t cnt) MANAPIHTTP_NOEXCEPT {
    current_stack_cnt = cnt;
}

std::size_t manapi::async::internal::max_stack_depth_crt () MANAPIHTTP_NOEXCEPT {
    return max_stack_depth_;
}

void manapi::async::internal::max_stack_depth_set (std::size_t cnt) MANAPIHTTP_NOEXCEPT {
    max_stack_depth_ = cnt;
}

bool manapi::async::internal::future_final_awaiter_ready() MANAPIHTTP_NOEXCEPT {
    return false;
}

void manapi::async::internal::append_static_task(manapi::fixed_function<void()> callback) MANAPIHTTP_NOEXCEPT {
    async::current()->etaskpool()->append_static_task(std::move(callback));
}

std::coroutine_handle<> manapi::async::internal::future_final_awaiter_suspend(std::coroutine_handle<promise<void, manapi::future<>>> handle) MANAPIHTTP_NOEXCEPT {
    auto promise_ = &handle.promise();
    auto waiting = std::exchange(promise_->waiting, nullptr);

    if (promise_->finish_cb) {
        promise_->finish_cb->operator()(std::move(promise_->exception));
    }

    return waiting ? waiting : std::noop_coroutine();
}

manapi::async::internal::promise_base_future::promise_base_future() = default;

manapi::async::internal::promise_base_future::~promise_base_future() = default;

void manapi::async::internal::promise_base_future::unhandled_exception() {
    this->exception = std::current_exception();
}

void manapi::async::internal::future_final_awaiter_suspend(std::coroutine_handle<promise_base_future> original, std::coroutine_handle<promise_base_future> handle) MANAPIHTTP_NOEXCEPT {
    auto &promise = original.promise();
    //auto &npromise = handle.promise();

    promise.waiting = handle;

    auto current_stack_cnt_ = manapi::async::internal::current_stack_cnt_crt ();
    if (current_stack_cnt_ >= async::internal::max_stack_depth_crt()) {
        auto &thr = manapi::async::current();
        if (thr) {
            async::internal::ethreadpool_(thr)->append_static_task([original] () -> void {
                 original.resume();
            });
        }

        return;
    }

    manapi::async::internal::current_stack_cnt_set (current_stack_cnt_ + 1);
    original.resume();
}


manapi::async::internal::promise<void, manapi::future<>>::promise() : manapi::async::internal::promise_base_future () {

}

manapi::async::internal::promise<void, manapi::future<>>::promise(promise &&n) MANAPIHTTP_NOEXCEPT : promise_base_future(std::forward<decltype(n)>(n)) {

}

void manapi::async::internal::promise<void, manapi::future<>>::return_void() {

}

manapi::async::internal::final_awaiter<void, manapi::async::internal::promise<void, manapi::future<>>> manapi::async::internal::promise<void, manapi::future<>>::promise::final_suspend() MANAPIHTTP_NOEXCEPT {
    return {};
}

std::suspend_always manapi::async::internal::promise<void, manapi::future<>>::promise::initial_suspend() {
    return {};
}

manapi::future<> manapi::async::internal::promise<void, manapi::future<>>::promise::get_return_object()  {
    return future<void>{ std::coroutine_handle<promise>::from_promise(*this) };
}








