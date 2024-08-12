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
        threadpool(size_t thread_num = 20);
        ~threadpool();
        bool append_task (T *task);
        void start();
        void stop();
        size_t get_count_stopped_task ();
        bool all_tasks_stopped ();
    private:
        size_t thread_number;
        pthread_t *all_threads;
        std::queue<T *> task_queue;
        mutex_locker queue_mutex_locker;
        cond_locker queue_cond_locker;
        // the function that the thread runs. Execute run() function
        static void *worker(void *arg);
        void run();
        T *getTask();
        bool is_stop;

        std::mutex m;
        std::condition_variable cv;

        std::atomic<size_t> stopped;
    };
}

#endif //MANAPIHTTP_MANAPITHREADPOOL_H
