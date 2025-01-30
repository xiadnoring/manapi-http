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
        std::shared_ptr<size_t> token;
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
        future<size_t> async_append_timer_sync (size_t ms, std::function<void()> task);
        future<size_t> async_append_timer_async (size_t ms, std::function<future<void>()> task);
        size_t append_timer_sync (size_t ms, std::function<void()> task);
        size_t append_timer_async (size_t ms, std::function<manapi::future<>()> task);
        future<void> async_remove_timer (size_t id);
        void remove_timer (size_t id);
        future<size_t> async_append_interval_sync (size_t ms, std::function<void()> task);
        future<size_t> async_append_interval_async (size_t ms, std::function<future<>()> task);
        size_t append_interval_async (size_t ms, std::function<manapi::future<>()> task);
        size_t append_interval_sync (size_t ms, std::function<void()> task);
        future<void> start (std::shared_ptr<timerpool> tp);
        future<void> stop ();
        void doit ();
        void clear();
        [[nodiscard]] std::shared_ptr<threadpool<task>> get_task_pool () const;
    protected:
        size_t _cb_event (struct adding_timer_data_t data);
        void _erase_task (const size_t &id);
        storage::iterator _erase_task (storage::iterator task);
        sorted_storage::iterator _erase_task (sorted_storage::iterator sorted_task);
        void _call_cb (storage::iterator task, std::shared_ptr<std::function<void()>> cb);
        void _async_call_cb (storage::iterator task, std::shared_ptr<std::function<future<void>()>> cb);
        void _start ();
        void flush_stack_free ();
        std::function<void()> sleep ();
        // wait while deps being exists
        std::atomic <size_t> deps;
        std::shared_ptr<async::condition_variable> cv;

        void _update_interval_state (const size_t& id);
        size_t _append (std::chrono::milliseconds duration, std::function<future<>()> async_task, std::function<void()> task,  bool inteval);
        sorted_storage sorted_tasks;
        storage tasks{};
        std::shared_ptr<event_loop> events{nullptr};
        std::shared_ptr<threadpool<task>> taskpool{nullptr};
        std::shared_ptr<async::mutex> smx;
        size_t index = 1;
        std::atomic<bool> _stop = false;
        double delay{};
        std::shared_ptr<ev::timer> timer;
        size_t finish_event{0};
    private:
    };
}
