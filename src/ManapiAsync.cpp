#include "ManapiAsync.hpp"
#include "ManapiThreadPool.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "ManapiInitTools.hpp"

struct current_data_t {
    std::size_t st_cnt = 0;
    std::size_t st_max = 15;
    std::shared_ptr<manapi::async::cthread> cthread;
    std::coroutine_handle<> root;
};

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
thread_local current_data_t ctxcurrent;

void manapi::init_tools::max_coro_stack(std::size_t sz) MANAPIHTTP_NOEXCEPT {
    ::ctxcurrent.st_max = sz;
}

void manapi::async::internal::current_(std::shared_ptr<cthread> ctx) MANAPIHTTP_NOEXCEPT {
    ctxcurrent.cthread = std::move(ctx);
}

const std::shared_ptr<manapi::async::cthread> & manapi::async::internal::current_() MANAPIHTTP_NOEXCEPT {
    return ctxcurrent.cthread;
}

std::size_t manapi::async::internal::current_stack_cnt_crt () MANAPIHTTP_NOEXCEPT {
    return ctxcurrent.st_cnt;
}

void manapi::async::internal::current_stack_cnt_set (std::size_t cnt) MANAPIHTTP_NOEXCEPT {
    ctxcurrent.st_cnt = cnt;
}

std::size_t manapi::async::internal::max_stack_depth_crt () MANAPIHTTP_NOEXCEPT {
    return ::ctxcurrent.st_max;
}

// void manapi::async::internal::cnt_finish_inc() MANAPIHTTP_NOEXCEPT {
//     // current_finish_cnt++;
// }

bool manapi::async::internal::future_final_awaiter_ready() MANAPIHTTP_NOEXCEPT {
    return false;
}

void manapi::async::internal::append_static_task(manapi::fixed_function<void()> callback) MANAPIHTTP_NOEXCEPT {
    async::current()->etaskpool()->append_static_task(std::move(callback));
}

void manapi::async::internal::append_task(std::move_only_function<void()> callback) MANAPIHTTP_NOEXCEPT {
    async::current()->etaskpool()->append_task(std::move(callback));

}

std::coroutine_handle<> manapi::async::internal::future_final_awaiter_suspend(std::coroutine_handle<promise<void, manapi::future<>>> handle) MANAPIHTTP_NOEXCEPT {
    auto promise_ = &handle.promise();
    auto waiting = std::exchange(promise_->waiting, nullptr);
    if (!waiting)
        waiting = std::noop_coroutine();
    // current_finish_cnt++;
    promise_->run_finish_cb();
    return waiting;
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
                 async::coro_resume(original);
            });
        }

        return;
    }

    manapi::async::internal::current_stack_cnt_set (current_stack_cnt_ + 1);
    async::coro_resume(original);
    manapi::async::internal::current_stack_cnt_set (current_stack_cnt_);
}


manapi::async::internal::promise<void, manapi::future<>>::promise() : manapi::async::internal::promise_base_future () {

}

manapi::async::internal::promise<void, manapi::future<>>::promise(promise &&n) MANAPIHTTP_NOEXCEPT : promise_base_future(std::forward<decltype(n)>(n)) {

}

manapi::async::internal::promise<void, manapi::future<>>::~promise() = default;

void manapi::async::internal::promise<void, manapi::future<>>::return_void() {

}

void manapi::async::internal::promise<void, manapi::future<>>::run_finish_cb() MANAPIHTTP_NOEXCEPT {
    if (this->finish_cb) {
        this->finish_cb->operator()(std::move(this->exception));
    }
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


void manapi::async::coro_resume(std::coroutine_handle<> handle) {
    assert(handle);

    if (::ctxcurrent.root) {
        handle.resume();
    }
    else {
        try {
            handle.resume();

            if (::ctxcurrent.root) {
                std::exchange(::ctxcurrent.root, nullptr).destroy();
            }
        }
        catch (std::exception const &) {
            if (::ctxcurrent.root) {
                std::exchange(::ctxcurrent.root, nullptr).destroy();
            }

            std::rethrow_exception(std::current_exception());
        }
    }

    // ++current_finish_cnt;
    // std::size_t const fin_cnt = current_finish_cnt;
    // handle.resume();
    // auto res = static_cast <internal::promise_base_future *> (handle.address());
    // assert(res);
    // if (fin_cnt!=current_finish_cnt) {
    //     assert(current_finish_cnt > fin_cnt);
    //     current_finish_cnt--;
    //     auto z = handle.done();
    //     res->run_finish_cb();
    // }
    // --current_finish_cnt;
}

void manapi::async::coro_finish(std::coroutine_handle<> handle) MANAPIHTTP_NOEXCEPT {
    assert(!::ctxcurrent.root);
    ::ctxcurrent.root = handle;
}
