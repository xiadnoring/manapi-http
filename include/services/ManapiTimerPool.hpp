#pragma once

#include <chrono>
#include <functional>
#include <set>
#include <map>

#include "../components/ManapiEventStructures.hpp"
#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "./ManapiEventLoop.hpp"
#include "./ManapiThreadPool.hpp"
#include "./ManapiTask.hpp"
#include "../async/ManapiAsyncMutex.hpp"
#include "../async/ManapiAsyncConditionVariable.hpp"
#include "../components/TimerObject.hpp"

namespace manapi {
    struct timer_task {
        std::chrono::milliseconds delay;
        std::chrono::steady_clock::time_point point;
        manapi::timer timer;
        bool interval;
        bool active;
    };
    class timerpool : public task {
    public:
        struct sorted_tasks_compare_t {
            bool operator()(const std::pair <std::chrono::steady_clock::time_point, size_t> &a, const std::pair <std::chrono::steady_clock::time_point, size_t> &b) const {
                return a.first < b.first;
            }
        };

        typedef std::map<size_t, timer_task> storage;
        typedef std::set <std::pair <std::chrono::steady_clock::time_point, size_t>, sorted_tasks_compare_t> sorted_storage;
        explicit timerpool(std::shared_ptr<event_loop> events, ssize_t delay = 0.01);
        ~timerpool();
        future<manapi::timer> async_append_timer_sync (size_t ms, std::move_only_function<void(manapi::timer t)> task);
        future<manapi::timer> async_append_timer_async (size_t ms, std::move_only_function<future<void>(manapi::timer t)> task);
        manapi::timer append_timer_sync (size_t ms, std::move_only_function<void(manapi::timer t)> task);
        manapi::timer append_timer_async (size_t ms, std::move_only_function<manapi::future<>(manapi::timer t)> task);
        future<void> async_remove_timer (size_t id);
        void remove_timer (size_t id);
        future<manapi::timer> async_append_interval_sync (size_t ms, std::move_only_function<void(manapi::timer t)> task);
        future<manapi::timer> async_append_interval_async (size_t ms, std::move_only_function<future<>(manapi::timer t)> task);
        manapi::timer append_interval_async (size_t ms, std::move_only_function<manapi::future<>(manapi::timer t)> task);
        manapi::timer append_interval_sync (size_t ms, std::move_only_function<void(manapi::timer t)> task);
        future<void> start (std::shared_ptr<timerpool> tp);
        future<void> stop ();
        void doit ();
        void clear();
        [[nodiscard]] std::shared_ptr<threadpool<task>> get_task_pool () const;
    protected:
        future<void> stop_ (bool evloop);
        std::optional<manapi::timer> _cb_event (void *data);
        void _erase_task (const size_t &id);
        storage::iterator _erase_task (storage::iterator task);
        sorted_storage::iterator _erase_task (sorted_storage::iterator sorted_task);
        void _async_call_cb (storage::iterator task, std::shared_ptr<std::move_only_function<future<void>()>> cb);
        void _start ();
        void flush_stack_free ();
        std::move_only_function<void()> sleep ();
        // wait while deps being exists
        std::atomic <size_t> deps;
        std::shared_ptr<async::condition_variable> cv;
        manapi::chain<size_t> prepare_remove;

        void _update_interval_state (const size_t& id);
        manapi::timer _append (std::chrono::milliseconds duration, std::move_only_function<future<>(manapi::timer t)> async_task, std::move_only_function<void(manapi::timer t)> task,  bool inteval);
        sorted_storage sorted_tasks;
        storage tasks{};
        std::shared_ptr<event_loop> events{nullptr};
        std::shared_ptr<threadpool<task>> taskpool{nullptr};
        std::shared_ptr<async::mutex> smx;
        std::atomic<bool> _stop = false;
        uint64_t delay{};
        std::shared_ptr<ev::timer> timer;
        size_t finish_event{0};
        bool timer_loop_running = false;
    private:
    };
}
