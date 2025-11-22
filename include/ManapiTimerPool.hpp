#pragma once

#include <chrono>
#include <functional>
#include <set>
#include <map>

#include "./ManapiUtils.hpp"
#include "./ManapiAsync.hpp"
#include "./std/ManapiAsyncMutex.hpp"
#include "./std/ManapiAsyncConditionVariable.hpp"
#include "./ManapiTimerObject.hpp"
#include "./ManapiErrors.hpp"
#include "./ManapiEventStructures.hpp"

namespace manapi {
    class timerpool {
        friend timer;
    public:
        typedef std::pair <std::chrono::steady_clock::time_point, std::shared_ptr<timer::timer_data_t>> sorted_storage_key;
        struct sorted_tasks_compare_t {
            bool operator()(const sorted_storage_key &a, const sorted_storage_key &b) const MANAPIHTTP_NOEXCEPT;
        };

        typedef std::set <sorted_storage_key, sorted_tasks_compare_t> sorted_storage;

        struct data_t;

        timerpool();

        /**
         * timerpool based on libuv timer event
         * @param events event loop
         */
        static manapi::error::status_or<timerpool> create (std::shared_ptr<event_loop> events) MANAPIHTTP_NOEXCEPT;

        /* deconstructor */
        ~timerpool();

        /* move */
        timerpool (timerpool &&n) MANAPIHTTP_NOEXCEPT;

        /* move */
        timerpool &operator=(timerpool &&n) MANAPIHTTP_NOEXCEPT;

        /* copy */
        timerpool (const timerpool &n);

        /* copy */
        timerpool &operator=(const timerpool &n);

        /**
         * Create a timeout event with synchronous callback
         * @param ms time in milliseconds
         * @param task synchronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_timer_sync (size_t ms, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT;


        /**
         * Create a timeout event with synchronous callback
         * @param ms time in milliseconds
         * @param task synchronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_timer_sync (size_t ms, timer_types type, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * Create a timeout event with asynchronous callback
         * @param ms time in milliseconds
         * @param task asynchronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_timer_async (size_t ms, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT;


        /**
         * Create a timeout event with asynchronous callback
         * @param ms time in milliseconds
         * @param task asynchronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_timer_async (size_t ms, timer_types type, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * FOR INTERNAL USE ONLY
         *
         * Remove a timer object from the pool using timer data
         * @param data timer data
         */
        void remove_timer (std::shared_ptr<timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT;

        /**
         * Create an interval event with an asynchronous callback
         * @param ms period in milliseconds
         * @param task asychronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_interval_async (size_t ms, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * Create an interval event with a synchronous callback
         * @param ms time in milliseconds
         * @param task sychronous callback
         * @return timer object on succes, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_interval_sync (size_t ms, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * Create an interval event with an asynchronous callback
         * @param ms period in milliseconds
         * @param task asychronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_interval_async (size_t ms, timer_types type, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * Create an interval event with a synchronous callback
         * @param ms time in milliseconds
         * @param task sychronous callback
         * @return timer object on succes, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status_or<manapi::timer> append_interval_sync (size_t ms, timer_types type, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT;


        /**
         * set the timer again
         * @param data timer data
         * @return Ok on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::error::status again_timer (std::shared_ptr<manapi::timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT;

        manapi::sys_error::status start () MANAPIHTTP_NOEXCEPT;

        void stop () MANAPIHTTP_NOEXCEPT;

        void run_once () MANAPIHTTP_NOEXCEPT;

        void clear() MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::shared_ptr<threadpool> taskpool () const MANAPIHTTP_NOEXCEPT;
    private:
        static void stop_ (std::shared_ptr<data_t> data, bool evloop) MANAPIHTTP_NOEXCEPT;
        //
        // std::optional<manapi::timer> _cb_event (void *data);

        static void erase_task_ (const std::shared_ptr<data_t> &data_,sorted_storage::iterator sorted_task) MANAPIHTTP_NOEXCEPT;

        static void start_ (const std::shared_ptr<data_t> &data) MANAPIHTTP_NOEXCEPT;

        static void flush_stack_free (const std::shared_ptr<data_t> &data_);

        static int64_t calculate_repeat_ (const std::shared_ptr<data_t> &data_);

        static bool reinit_timer_ (const std::shared_ptr<data_t> &data_) MANAPIHTTP_NOEXCEPT;

        static manapi::error::status update_interval_state_ (const std::shared_ptr<data_t> &data_, std::shared_ptr<manapi::timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT;

        manapi::sys_error::status init_timer_ () MANAPIHTTP_NOEXCEPT;

        manapi::error::status_or<manapi::timer> append_ (std::chrono::milliseconds duration, manapi::timer::async_cb_t async_task, manapi::timer::sync_cb_t task,  bool interval, timer_types type) MANAPIHTTP_NOEXCEPT;

        manapi::error::status update_interval_state (std::shared_ptr<timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT;

        void unref_important_ () MANAPIHTTP_NOEXCEPT;

        std::shared_ptr<data_t> data_;
    };
}
