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

#if defined (__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

#include "../ManapiUtils.hpp"
#include "../ManapiErrors.hpp"
#include "../ManapiJson.hpp"
#include "../ManapiTime.hpp"
#include "../ManapiDebug.hpp"
#include "../components/ManapiChain.hpp"

namespace manapi {
    template <class T>
    class threadpool {
    public:
        threadpool(ssize_t thread_num = 20, ssize_t queues_count = 3);
        ~threadpool();
        void resize (ssize_t thread_num);
        bool append_task (std::unique_ptr<T> task, int level = 0);
        void append_task (T task);
        void append_task (std::move_only_function<void()> cb);
        void start();
        void stop();
        [[nodiscard]] std::size_t size () const;
        void clear();
        void join();
        void for_all_threads (std::function<void()> cb);
        bool try_todo_task ();
    private:
        // this vector contains all threads for this thread pool
        std::vector <std::thread> threads;
        // this vector of queue which contains tasks
        std::vector <chain <std::unique_ptr<T> > > task_queues;
        // queue mutex
        std::mutex queue_mutex;
        // the function that the thread runs. Execute run() function
        static void *worker(void *arg, ssize_t index);

        void run(ssize_t index);
        // execute the task
        void task_doit (std::unique_ptr<T> task);
        std::unique_ptr<T> get_task(ssize_t index);
        std::atomic<bool> is_stop;
        ssize_t threadnum;

#if defined(__unix__)||defined(__APPLE__)
        sigset_t blockedSignal{};
#endif
        std::mutex m;
        std::condition_variable cv;
        std::vector<chain<std::unique_ptr<T>>> tasks_by_thread;
    };
}
