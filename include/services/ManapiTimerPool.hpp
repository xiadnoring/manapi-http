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
            bool operator()(const sorted_storage_key &a, const sorted_storage_key &b) const MANAPIHTTP_NOEXCEPT;
        };

        typedef std::set <sorted_storage_key, sorted_tasks_compare_t> sorted_storage;

        struct data_t;

        /**
         * timerpool based on libuv timer event
         * @param events event loop
         */
        timerpool(std::shared_ptr<event_loop> events);

        /* deconstructor */
        ~timerpool();

        /* move */
        timerpool (timerpool &&n) noexcept;

        /* move */
        timerpool &operator=(timerpool &&n) noexcept;

        /* copy */
        timerpool (const timerpool &n);

        /* copy */
        timerpool &operator=(const timerpool &n);

        /**
         * Create a timeout event using synchronous callback
         * @param ms time in milliseconds
         * @param task synchronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_timer_sync (size_t ms, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * Create a timeout event using asynchronous callback
         * @param ms time in milliseconds
         * @param task asynchronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_timer_async (size_t ms, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * FOR INTERNAL USE ONLY
         *
         * Remove a timer object from the pool using timer data
         * @param data timer data
         */
        void remove_timer (std::shared_ptr<timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT;

        /**
         * Create an interval event using an asynchronous callback
         * @param ms period in milliseconds
         * @param task asychronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_interval_async (size_t ms, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * Create an interval event using a synchronous callback
         * @param ms time in milliseconds
         * @param task sychronous callback
         * @return timer object on succes, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_interval_sync (size_t ms, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * FOR INTERNAL USE ONLY
         *
         * @param data
         * @return
         */
        manapi::error::status update_interval_state (std::shared_ptr<timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT;

        /**
         * set the timer again
         * @param data timer data
         * @return Ok on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status again_timer (std::shared_ptr<manapi::timer::timer_data_t> data);

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

        static void erase_task_ (const std::shared_ptr<data_t> &data_,sorted_storage::iterator sorted_task) MANAPIHTTP_NOEXCEPT;

        static void start_ (const std::shared_ptr<data_t> &data);

        static void flush_stack_free (const std::shared_ptr<data_t> &data_);

        static int64_t calculate_repeat_ (const std::shared_ptr<data_t> &data_);

        static bool reinit_timer_ (const std::shared_ptr<data_t> &data_) MANAPIHTTP_NOEXCEPT;

        static manapi::error::status update_interval_state_ (const std::shared_ptr<data_t> &data_, std::shared_ptr<manapi::timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<manapi::timer> append_ (std::chrono::milliseconds duration, manapi::timer::async_cb_t async_task, manapi::timer::sync_cb_t task,  bool interval) MANAPIHTTP_NOEXCEPT;

        std::shared_ptr<data_t> data_;
    private:
    };
}
