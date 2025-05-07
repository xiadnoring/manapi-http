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

#include "../ManapiUtils.hpp"
#include "../ManapiErrors.hpp"
#include "../ManapiJson.hpp"
#include "../ManapiTime.hpp"
#include "../components/ManapiChain.hpp"
#include "../async/ManapiAsyncLogger.hpp"

namespace manapi {
    template <class T>
    class threadpool {
    public:
        threadpool(std::shared_ptr<manapi::logger> logger) : logger_(std::move(logger)) {}

        virtual ~threadpool() = default;

        virtual void append_task (std::unique_ptr<T> task) = 0;

        virtual void append_task (T task) = 0;

        virtual void append_task (std::move_only_function<void()> cb) = 0;

        virtual void start() = 0;

        virtual void stop() = 0;

        virtual void join () = 0;

        const std::shared_ptr<manapi::logger> &logger() {
            return this->logger_;
        }
    protected:
        std::shared_ptr<manapi::logger> logger_;

    };

    template<class T>
    class mthreadpool : public threadpool<T> {
    public:
        mthreadpool(std::shared_ptr<manapi::logger> logger, ssize_t thread_num);
        ~mthreadpool();

        [[nodiscard]] std::size_t size () const;

        void resize (ssize_t thread_num);

        void clear();

        void join() override;

        void for_all_threads (std::function<void()> cb);

        void stop () override;

        void start () override;

        void append_task (std::unique_ptr<T> task) override;

        void append_task (T task) override;

        void append_task (std::move_only_function<void()> cb) override;

    private:
        // this vector contains all threads for this thread pool
        std::vector <std::thread> threads;

        std::mutex m;

        std::condition_variable cv;

        std::vector<chain<std::unique_ptr<T>>> tasks_by_thread;

        // this vector of queue which contains tasks
        chain <std::unique_ptr<T> > tasks;
        // queue mutex
        std::mutex queue_mutex;
        // the function that the thread runs. Execute run() function
        static void *worker(void *arg, ssize_t index);

        void run(ssize_t index);

        std::unique_ptr<T> get_task(ssize_t index);

        std::atomic<int> flags;

        ssize_t threadnum;
    };

    template<class T>
    class ethreadpool : public threadpool<T> {
    public:
        ethreadpool (std::shared_ptr<manapi::logger> logger, std::move_only_function<void()> ontask);

        ~ethreadpool ();

        bool try_task ();

        void set_notify ();

        void stop () override;

        void start () override;

        void append_task (std::unique_ptr<T> task) override;

        void append_task (T task) override;

        void append_task (std::move_only_function<void()> cb) override;

        void join () override;
    private:
        int flags_;

        chain <std::unique_ptr<T> > tasks;

        std::move_only_function<void()> ontask_;
    };
}
