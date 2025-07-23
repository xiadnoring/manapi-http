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
#include "../components/ManapiTimerObject.hpp"

namespace manapi {
    class timerpool : public task {
    public:
        typedef std::pair <std::chrono::steady_clock::time_point, std::shared_ptr<timer::timer_data_t>> sorted_storage_key;
        struct sorted_tasks_compare_t {
            bool operator()(const sorted_storage_key &a, const sorted_storage_key &b) const {
                return a.first < b.first;
            }
        };

        typedef std::set <sorted_storage_key, sorted_tasks_compare_t> sorted_storage;

        struct data_t {
            sorted_storage sorted_tasks;
            std::shared_ptr<event_loop> events{nullptr};
            int flags;
            std::shared_ptr<ev::timer> timer;
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

        manapi::timer append_timer_sync (size_t ms, manapi::timer::sync_cb_t task);

        manapi::timer append_timer_async (size_t ms, manapi::timer::async_cb_t task);

        // future<void> async_remove_timer (size_t id);

        void remove_timer (std::shared_ptr<timer::timer_data_t> data) MANAPIHTTP_NOEXPECT;

        // future<manapi::timer> async_append_interval_sync (size_t ms, std::move_only_function<void(manapi::timer t)> task);
        //
        // future<manapi::timer> async_append_interval_async (size_t ms, std::move_only_function<future<>(manapi::timer t)> task);

        manapi::timer append_interval_async (size_t ms, manapi::timer::async_cb_t task);

        manapi::timer append_interval_sync (size_t ms, manapi::timer::sync_cb_t task);

        void update_interval_state (std::shared_ptr<timer::timer_data_t> data);

        void again_timer (std::shared_ptr<manapi::timer::timer_data_t> data);

        void start ();

        void stop ();

        void doit ();

        void run_once ();

        void clear();

        [[nodiscard]] std::shared_ptr<threadpool<task>> taskpool () const;
    protected:
        static void stop_ (std::shared_ptr<data_t> data, bool evloop);
        //
        // std::optional<manapi::timer> _cb_event (void *data);

        static void erase_task_ (const std::shared_ptr<data_t> &data_,sorted_storage::iterator sorted_task) MANAPIHTTP_NOEXPECT;

        static void start_ (const std::shared_ptr<data_t> &data);

        static void flush_stack_free (const std::shared_ptr<data_t> &data_);

        static int64_t calculate_repeat_ (const std::shared_ptr<data_t> &data_);

        static bool reinit_timer_ (const std::shared_ptr<data_t> &data_) MANAPIHTTP_NOEXPECT;

        static void update_interval_state_ (const std::shared_ptr<data_t> &data_, std::shared_ptr<manapi::timer::timer_data_t> data);

        manapi::timer append_ (std::chrono::milliseconds duration, manapi::timer::async_cb_t async_task, manapi::timer::sync_cb_t task,  bool interval);

        std::shared_ptr<data_t> data_;
    private:
    };
}
