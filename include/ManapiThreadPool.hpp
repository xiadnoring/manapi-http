#pragma once

#include <queue>
#include <deque>
#include <cstdio>
#include <exception>
#include <cerrno>
#include <iostream>
#include <mutex>
#include <coroutine>
#include <condition_variable>
#include <functional>

#include "./ManapiUtils.hpp"
#include "./ManapiErrors.hpp"
#include "./json/ManapiJson.hpp"
#include "./ManapiTime.hpp"
#include "./std/ManapiFunction.hpp"
#include "./std/ManapiChain.hpp"
#include "./std/ManapiAsyncLogger.hpp"

namespace manapi {
    class threadpool {
    public:
        threadpool(std::shared_ptr<manapi::logger> logger) : logger_(std::move(logger)) {}

        virtual ~threadpool() = default;

        virtual void append_task (std::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT = 0;

        virtual void append_super_task (manapi::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT = 0;

        virtual void start() = 0;

        virtual void stop() MANAPIHTTP_NOEXCEPT = 0;

        virtual void join () MANAPIHTTP_NOEXCEPT = 0;

        MANAPIHTTP_NODISCARD virtual std::size_t tasks_size () const MANAPIHTTP_NOEXCEPT = 0;

        const std::shared_ptr<manapi::logger> &logger () MANAPIHTTP_NOEXCEPT {
            return this->logger_;
        }
    protected:
        std::shared_ptr<manapi::logger> logger_;
    };

    class DLLExportImport mthreadpool : public threadpool {
    public:
        typedef std::vector<chain<std::move_only_function<void()>>> tasks_by_thread_t;

        mthreadpool(std::shared_ptr<manapi::logger> logger, ssize_t thread_num);

        ~mthreadpool() override;

        void resize (ssize_t thread_num);

        void clear();

        void join() MANAPIHTTP_NOEXCEPT override;

        void for_all_threads (std::move_only_function<void(tasks_by_thread_t *)> cb);

        void stop () MANAPIHTTP_NOEXCEPT override;

        void start () override;

        void append_task (std::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT override;

        void append_super_task(manapi::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT override;

        std::size_t size() const MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::size_t tasks_size() const MANAPIHTTP_NOEXCEPT override;
    private:
        // this vector contains all threads for this thread pool
        std::vector <std::thread> threads;

        std::mutex m;

        std::condition_variable cv;

        tasks_by_thread_t tasks_by_thread;

        // this vector of queue which contains tasks
        std::deque <std::move_only_function<void()>> tasks;

        std::deque <manapi::move_only_function<void()>> tasks2;

        // queue mutex
        mutable std::mutex queue_mutex;

        // the function that the thread runs. Execute run() function
        static void *worker(void *arg, ssize_t index);

        void run(ssize_t index);

        int get_task(ssize_t index, std::move_only_function<void()> *cb1, manapi::move_only_function<void()> *cb2);

        std::atomic<int> flags;

        ssize_t threadnum;
    };

    class DLLExportImport ethreadpool : public threadpool {
    public:
        ethreadpool (std::shared_ptr<manapi::logger> logger, std::move_only_function<void()> ontask);

        ~ethreadpool () override;

        MANAPIHTTP_NODISCARD std::size_t tasks_size() const MANAPIHTTP_NOEXCEPT override;

        bool try_task ();

        void set_notify () MANAPIHTTP_NOEXCEPT;

        void set_notify_cb (std::move_only_function<void()> ontask) MANAPIHTTP_NOEXCEPT;

        void stop () MANAPIHTTP_NOEXCEPT override;

        void start () override;

        void append_task (std::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT override;

        void append_super_task(manapi::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT override;

        void join () MANAPIHTTP_NOEXCEPT override;
    private:
        int flags_;

        std::deque <std::move_only_function<void()>> tasks;

        std::deque <manapi::move_only_function<void()>> tasks2;

        std::move_only_function<void()> ontask_;
    };
}
