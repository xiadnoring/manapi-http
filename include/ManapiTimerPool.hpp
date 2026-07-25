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
         */
        static manapi::status_or<std::shared_ptr<timerpool>> create () MANAPIHTTP_NOEXCEPT;

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
        manapi::status_or<manapi::timer> append_timer_sync (size_t ms, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT;


        /**
         * Create a timeout event with synchronous callback
         * @param ms time in milliseconds
         * @param task synchronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::status_or<manapi::timer> append_timer_sync (size_t ms, timer_types type, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * Create a timeout event with asynchronous callback
         * @param ms time in milliseconds
         * @param task asynchronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::status_or<manapi::timer> append_timer_async (size_t ms, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT;


        /**
         * Create a timeout event with asynchronous callback
         * @param ms time in milliseconds
         * @param task asynchronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::status_or<manapi::timer> append_timer_async (size_t ms, timer_types type, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT;

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
        manapi::status_or<manapi::timer> append_interval_async (size_t ms, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * Create an interval event with a synchronous callback
         * @param ms time in milliseconds
         * @param task sychronous callback
         * @return timer object on succes, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::status_or<manapi::timer> append_interval_sync (size_t ms, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * Create an interval event with an asynchronous callback
         * @param ms period in milliseconds
         * @param task asychronous callback
         * @return timer object on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::status_or<manapi::timer> append_interval_async (size_t ms, timer_types type, manapi::timer::async_cb_t task) MANAPIHTTP_NOEXCEPT;

        /**
         * Create an interval event with a synchronous callback
         * @param ms time in milliseconds
         * @param task sychronous callback
         * @return timer object on succes, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::status_or<manapi::timer> append_interval_sync (size_t ms, timer_types type, manapi::timer::sync_cb_t task) MANAPIHTTP_NOEXCEPT;


        /**
         * set the timer again
         * @param data timer data
         * @return Ok on success, otherwise it returns InternalError, ResourceExhausted
         */
        manapi::status again_timer (std::shared_ptr<manapi::timer::timer_data_t> data) MANAPIHTTP_NOEXCEPT;

        manapi::ev::status start () MANAPIHTTP_NOEXCEPT;

        void stop () MANAPIHTTP_NOEXCEPT;

        void run_once () MANAPIHTTP_NOEXCEPT;

        void clear() MANAPIHTTP_NOEXCEPT;

        const std::shared_ptr<data_t> &data ();
    private:
        std::shared_ptr<data_t> m_data;
    };
}
