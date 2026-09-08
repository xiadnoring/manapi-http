#pragma once

#include <iostream>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <coroutine>

#include "./ManapiUtils.hpp"
#include "./ManapiTime.hpp"
#include "./std/ManapiFunction.hpp"
#include "./std/ManapiChain.hpp"
#include "./std/ManapiLogger.hpp"

namespace manapi {
    enum task_types {
        TASK_TYPE_FUNC = 0,
        TASK_TYPE_HANDLE
    };

    class mthreadpool {
    public:
        struct data_t;

        typedef std::vector<chain<std::move_only_function<void()>>> tasks_by_thread_t;

        mthreadpool(std::size_t thread_num);

        ~mthreadpool();

        void resize (std::size_t thread_num);

        void clear();

        void join() MANAPIHTTP_NOEXCEPT;

        void for_all_threads (std::move_only_function<void(tasks_by_thread_t *)> &&cb);

        void stop () MANAPIHTTP_NOEXCEPT;

        void notify_all () MANAPIHTTP_NOEXCEPT;

        void start ();

        void append_task (std::move_only_function<void()>&& cb);

        MANAPIHTTP_NODISCARD std::size_t size() const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::size_t tasks_size() const MANAPIHTTP_NOEXCEPT;

        void reserve_tasks(manapi::task_types type, std::size_t sz);

        void release_task (std::move_only_function<void()>&& cb) MANAPIHTTP_NOEXCEPT;

        void task ();
    private:
        std::unique_ptr<data_t> m_data;
    };

    class ethreadpool {
    public:
        struct data_t;

        ethreadpool ();

        ~ethreadpool ();

        MANAPIHTTP_NODISCARD std::size_t tasks_size() const MANAPIHTTP_NOEXCEPT;

        void reserve_tasks (manapi::task_types type, std::size_t sz);

        void release_tasks(manapi::task_types type, std::size_t sz) MANAPIHTTP_NOEXCEPT;

        bool try_task ();

        void set_notify () MANAPIHTTP_NOEXCEPT;

        void set_notify_cb (std::move_only_function<void()> ontask) MANAPIHTTP_NOEXCEPT;

        void stop () MANAPIHTTP_NOEXCEPT;

        void start ();

        void append_task (std::move_only_function<void()> &&cb);

        void append_task(const std::coroutine_handle<>& handle);

        void join () MANAPIHTTP_NOEXCEPT;
    private:
        std::unique_ptr<data_t> m_data;
    };
}
