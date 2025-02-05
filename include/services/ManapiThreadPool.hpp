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

#include "../components/ManapiChain.hpp"

namespace manapi {
    template <class T>
    class threadpool {
    public:
        threadpool(size_t thread_num = 20, size_t queues_count = 3);
        ~threadpool();
        void resize (size_t thread_num);
        bool append_task (std::unique_ptr<T> task, int level = 0);
        void append_task (T task);
        void append_task (std::function<void()> cb);
        void start();
        void stop();
        void clear();
        void join();
    private:
        // this vector contains all threads for this thread pool
        std::vector <std::thread> threads;
        // this vector of queue which contains tasks
        std::vector <chain <std::unique_ptr<T> > > task_queues;
        // queue mutex
        std::mutex queue_mutex;
        // the function that the thread runs. Execute run() function
        static void *worker(void *arg);

        void run();
        // execute the task
        void task_doit (std::unique_ptr<T> task);
        std::unique_ptr<T> get_task();
        std::atomic<bool> is_stop;
        size_t threadnum;

#if defined(__unix__)||defined(__APPLE__)
        sigset_t blockedSignal{};
#endif
        std::mutex m;
        std::condition_variable cv;
    };
}
