#ifndef MANAPIHTTP_MANAPITHREADPOOL_H
#define MANAPIHTTP_MANAPITHREADPOOL_H

#include <queue>
#include <cstdio>
#include <exception>
#include <cerrno>
#include <pthread.h>
#include <iostream>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include "ManapiLocker.h"

namespace manapi::net {
    template <class T>
    class threadpool {
    public:
        threadpool(size_t thread_num = 20, size_t queues_count = 3);
        ~threadpool();
        bool append_task (T *task, int level = 0);
        void start();
        void stop();
        size_t get_count_stopped_task ();
        bool all_tasks_stopped ();
    private:
        // this number means count of the all threads
        size_t thread_number;
        // this vector contains all threads for this thread pool
        std::vector <std::thread> all_threads;
        // this vector of queue which contains tasks
        std::vector <std::queue<T *>> task_queues;
        // queue mutex
        std::mutex queue_mutex;
        // the function that the thread runs. Execute run() function
        static void *worker(void *arg);
        void run();
        // execute the task
        void task_doit (T *task);
        T *getTask();
        bool is_stop;

        sigset_t blockedSignal{};
        std::mutex m;
        std::condition_variable cv;

        std::atomic<size_t> stopped;
    };
}

#endif //MANAPIHTTP_MANAPITHREADPOOL_H
