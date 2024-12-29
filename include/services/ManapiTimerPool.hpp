#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "ManapiAsync.hpp"
#include "ManapiThreadPool.hpp"
#include "ManapiTask.hpp"
#include "async/ManapiAsyncConditionVariable.hpp"
#include "async/ManapiAsyncMutex.hpp"

namespace manapi::net::utils {
    struct timer_task {
        std::chrono::milliseconds delay;
        std::shared_ptr<std::function <future<>()>> async_task;
        std::shared_ptr<std::function <void()>> task;
        std::chrono::time_point<std::chrono::high_resolution_clock> point;
        bool interval;
        bool enabled;
    };
    class timerpool : public net::task {
    public:
        explicit timerpool(std::shared_ptr<net::threadpool<net::task>> threadpool, const size_t &delay = 50);
        ~timerpool();
        future<size_t> async_append_timer_sync (const std::chrono::milliseconds &duration, const std::function<void()> &task);
        future<size_t> async_append_timer_async (const std::chrono::milliseconds &duration, const std::function<future<void>()> &task);
        size_t append_timer (const std::chrono::milliseconds &duration, const std::function<void()> &task);
        future<void> async_remove_timer (size_t id);
        void remove_timer (const size_t &id);
        future<size_t> async_append_interval_sync (const std::chrono::milliseconds &duration, const std::function<void()> &task);
        future<size_t> async_append_interval_async (const std::chrono::milliseconds &duration, const std::function<future<>()> &task);
        size_t append_interval (const std::chrono::milliseconds &duration, const std::function<void()> &task);
        void start ();
        future<> async_stop ();
        void stop ();
        void doit ();
        std::shared_ptr<threadpool<task>> get_threadpool ();
    protected:
        void _call_cb (std::unordered_map<size_t, timer_task>::iterator task, std::shared_ptr<std::function<void()>> cb);
        void _async_call_cb (std::unordered_map<size_t, timer_task>::iterator task, std::shared_ptr<std::function<future<void>()>> cb);
        future<void> _start ();
        std::function<void()> sleep ();
        // wait while deps being exists
        std::atomic <size_t> deps;
        async_condition_variable cv;

        future<void> _update_interval_state (const size_t& id);
        future<size_t> _append (const std::chrono::milliseconds &duration, const std::function<future<>()> &async_task, const std::function<void()> &task, const bool &inteval);
        std::unordered_map <size_t, timer_task> tasks;
        std::shared_ptr<net::threadpool<net::task>> taskpool;
        async_mutex mx;
        size_t index = 1;
        std::atomic<bool> _stop = false;
        size_t delay{};
    private:
    };
}
