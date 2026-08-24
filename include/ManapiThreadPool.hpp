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

    class threadpool {
    public:
        virtual void append_task (std::move_only_function<void()> &&cb) = 0;

        virtual void append_task (const std::coroutine_handle<> &handle) = 0;

        virtual void start() = 0;

        virtual void stop() MANAPIHTTP_NOEXCEPT = 0;

        virtual void join () MANAPIHTTP_NOEXCEPT = 0;

        virtual void reserve_tasks (task_types type, std::size_t sz) = 0;

        virtual void release_tasks (task_types type, std::size_t sz) MANAPIHTTP_NOEXCEPT = 0;

        MANAPIHTTP_NODISCARD virtual std::size_t tasks_size () const MANAPIHTTP_NOEXCEPT = 0;
    };

    class mthreadpool : public threadpool {
    public:
        struct data_t;

        typedef std::vector<chain<std::move_only_function<void()>>> tasks_by_thread_t;

        mthreadpool(std::size_t thread_num);

        ~mthreadpool();

        void resize (std::size_t thread_num);

        void clear();

        void join() MANAPIHTTP_NOEXCEPT override;

        void for_all_threads (std::move_only_function<void(tasks_by_thread_t *)> &&cb);

        void stop () MANAPIHTTP_NOEXCEPT override;

        void start () override;

        void append_task (std::move_only_function<void()>&& cb) override;

        void append_task (const std::coroutine_handle<> &handle) override;

        MANAPIHTTP_NODISCARD std::size_t size() const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::size_t tasks_size() const MANAPIHTTP_NOEXCEPT override;

        void reserve_tasks(manapi::task_types type, std::size_t sz) override;

        void release_tasks(manapi::task_types type, std::size_t sz) MANAPIHTTP_NOEXCEPT override;
    private:
        std::unique_ptr<data_t> m_data;
    };

    class ethreadpool : public threadpool {
    public:
        struct data_t;

        ethreadpool (std::shared_ptr<manapi::logger> logger, std::move_only_function<void()> ontask);

        ~ethreadpool ();

        MANAPIHTTP_NODISCARD std::size_t tasks_size() const MANAPIHTTP_NOEXCEPT override;

        void reserve_tasks (manapi::task_types type, std::size_t sz) override;

        void release_tasks(manapi::task_types type, std::size_t sz) MANAPIHTTP_NOEXCEPT override;

        bool try_task ();

        void set_notify () MANAPIHTTP_NOEXCEPT;

        void set_notify_cb (std::move_only_function<void()> ontask) MANAPIHTTP_NOEXCEPT;

        void stop () MANAPIHTTP_NOEXCEPT override;

        void start () override;

        void append_task (std::move_only_function<void()> &&cb) override;

        void append_task(const std::coroutine_handle<>& handle) override;

        void join () MANAPIHTTP_NOEXCEPT override;

        const std::shared_ptr<manapi::logger> &logger() MANAPIHTTP_NOEXCEPT;
    private:
        std::unique_ptr<data_t> m_data;
    };
}
