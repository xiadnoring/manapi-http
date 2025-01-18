#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <set>
#include <map>

#include "ManapiAsync.hpp"
#include "ManapiEventLoop.hpp"
#include "ManapiThreadPool.hpp"
#include "ManapiTask.hpp"
#include "async/ManapiAsyncMutex.hpp"
#include "async/ManapiAsyncConditionVariable.hpp"

namespace manapi {
    struct timer_task {
        std::chrono::milliseconds delay;
        std::shared_ptr<std::function <future<>()>> async_task;
        std::shared_ptr<std::function <void()>> task;
        std::chrono::steady_clock::time_point point;
        bool interval;
        bool enabled;
    };
    class timerpool : public task {
    public:
        struct sorted_tasks_compare_t {
            bool operator()(const std::pair <std::chrono::steady_clock::time_point, size_t> &a, const std::pair <std::chrono::steady_clock::time_point, size_t> &b) const {
                return a.first < b.first;
            }
        };

        typedef std::unordered_map<size_t, timer_task> storage;
        typedef std::set <std::pair <std::chrono::steady_clock::time_point, size_t>, sorted_tasks_compare_t> sorted_storage;
        explicit timerpool(std::shared_ptr<event_loop> events, const double &delay = 0.01);
        ~timerpool();
        future<size_t> async_append_timer_sync (const std::chrono::milliseconds &duration, std::function<void()> task);
        future<size_t> async_append_timer_async (const std::chrono::milliseconds &duration, std::function<future<void>()> task);
        size_t append_timer (const std::chrono::milliseconds &duration, const std::function<void()> &task);
        future<void> async_remove_timer (size_t id);
        void remove_timer (const size_t &id);
        future<size_t> async_append_interval_sync (const std::chrono::milliseconds &duration, std::function<void()> task);
        future<size_t> async_append_interval_async (const std::chrono::milliseconds &duration, std::function<future<>()> task);
        size_t append_interval (const std::chrono::milliseconds &duration, const std::function<void()> &task);
        future<void> start (std::shared_ptr<timerpool> tp);
        future<void> stop ();
        void doit ();
        void clear();
        [[nodiscard]] std::shared_ptr<threadpool<task>> get_task_pool () const;
    protected:
        void _erase_task (const size_t &id);
        storage::iterator _erase_task (storage::iterator task);
        sorted_storage::iterator _erase_task (sorted_storage::iterator sorted_task);
        void _call_cb (storage::iterator task, std::shared_ptr<std::function<void()>> cb);
        void _async_call_cb (storage::iterator task, std::shared_ptr<std::function<future<void>()>> cb);
        future<void> _start ();
        void flush_stack_free ();
        std::function<void()> sleep ();
        // wait while deps being exists
        std::atomic <size_t> deps;
        std::shared_ptr<async::condition_variable> cv;

        future<void> _update_interval_state (const size_t& id);
        future<size_t> _append (const std::chrono::milliseconds &duration, const std::function<future<>()> &async_task, const std::function<void()> &task, const bool &inteval);
        sorted_storage sorted_tasks;
        storage tasks{};
        std::shared_ptr<event_loop> events{nullptr};
        std::shared_ptr<threadpool<task>> taskpool{nullptr};
        std::shared_ptr<async::mutex> mx;
        std::shared_ptr<async::mutex> smx;
        size_t index = 1;
        std::atomic<bool> _stop = false;
        double delay{};
        std::shared_ptr<ev::timer> timer;
        size_t finish_event{0};
    private:
    };
}
