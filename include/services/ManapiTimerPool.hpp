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
        int flags;
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

        struct data_t {
            // wait while deps being exists
            manapi::chain<size_t> prepare_remove;

            sorted_storage sorted_tasks;
            storage tasks{};
            std::shared_ptr<event_loop> events{nullptr};
            int flags;
            std::shared_ptr<ev::timer> timer;
            size_t finish_event{0};
        };

        timerpool(std::shared_ptr<event_loop> events);

        ~timerpool();

        timerpool (timerpool &&n) noexcept;

        timerpool &operator=(timerpool &&n) noexcept;

        timerpool (const timerpool &n);

        timerpool &operator=(const timerpool &n);

        // future<manapi::timer> async_append_timer_sync (size_t ms, std::move_only_function<void(manapi::timer t)> task);
        //
        // future<manapi::timer> async_append_timer_async (size_t ms, std::move_only_function<future<void>(manapi::timer t)> task);

        manapi::timer append_timer_sync (size_t ms, std::move_only_function<void(manapi::timer t)> task);

        manapi::timer append_timer_async (size_t ms, std::move_only_function<manapi::future<>(manapi::timer t)> task);

        // future<void> async_remove_timer (size_t id);

        void remove_timer (size_t id);

        // future<manapi::timer> async_append_interval_sync (size_t ms, std::move_only_function<void(manapi::timer t)> task);
        //
        // future<manapi::timer> async_append_interval_async (size_t ms, std::move_only_function<future<>(manapi::timer t)> task);

        manapi::timer append_interval_async (size_t ms, std::move_only_function<manapi::future<>(manapi::timer t)> task);

        manapi::timer append_interval_sync (size_t ms, std::move_only_function<void(manapi::timer t)> task);

        void update_interval_state (std::size_t id);

        void start ();

        void stop ();

        void doit ();

        void clear();

        [[nodiscard]] std::shared_ptr<threadpool<task>> taskpool () const;
    protected:
        static void stop_ (std::shared_ptr<data_t> data, bool evloop);
        //
        // std::optional<manapi::timer> _cb_event (void *data);

        static void erase_task_ (const std::shared_ptr<data_t> &data_,const size_t &id);

        static storage::iterator erase_task_ (const std::shared_ptr<data_t> &data_,storage::iterator task);

        static sorted_storage::iterator erase_task_ (const std::shared_ptr<data_t> &data_,sorted_storage::iterator sorted_task);

        static void start_ (const std::shared_ptr<data_t> &data);

        static void flush_stack_free (const std::shared_ptr<data_t> &data_);

        static uint64_t calculate_repeat_ (const std::shared_ptr<data_t> &data_);

        static bool reinit_timer_ (const std::shared_ptr<data_t> &data_);

        static void update_interval_state_ (const std::shared_ptr<data_t> &data_, const size_t& id);

        manapi::timer append_ (std::chrono::milliseconds duration, std::move_only_function<future<>(manapi::timer t)> async_task, std::move_only_function<void(manapi::timer t)> task,  bool inteval);

        std::shared_ptr<data_t> data_;
    private:
    };
}
