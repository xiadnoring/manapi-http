#include "ManapiThreadPool.h"
#include "ManapiTask.h"

namespace manapi::net {
    template<class T>
    threadpool<T>::threadpool(size_t thread_num): thread_number(thread_num),is_stop(false),all_threads(nullptr) {
        if (thread_num <= 0)
            printf("threadpool cant init because thread_number = 0");

        all_threads = new pthread_t[thread_number];
        if (all_threads == nullptr)
            printf("cant init threadpool because thread array cant new");
    }

    template<class T>
    threadpool<T>::~threadpool() {
        delete []all_threads;
        stop();
    }

    template<class T>
    void threadpool<T>::stop() {
        is_stop = true;
        queue_cond_locker.broadcast();
    }

    template<class T>
    void threadpool<T>::start() {
        for (size_t i = 0; i < thread_number; ++i) {
            // failed to create thread, clear the successfully applied resources and throw an exception and
            // set the thread to leave the thread
            if (pthread_create(all_threads + i, nullptr, worker, this) != 0
                || pthread_detach(all_threads[i])) {
                delete []all_threads;
                throw std::exception();
            }
        }
    }

    template<class T>
    bool threadpool<T>::append_task(T *task) {
        // obtain a mutex
        queue_mutex_locker.mutex_lock();
        bool is_signal = task_queue.empty();
        // add into the queue
        task_queue.push(task);
        queue_mutex_locker.mutex_unlock();

        // wake up the thread waiting for the task
        if (is_signal)
            queue_cond_locker.signal();
        return true;
    }

    template<class T>
    T* threadpool<T>::getTask() {
        T *task = nullptr;
        queue_mutex_locker.mutex_lock();
        if (!task_queue.empty()) {
            task = task_queue.front();
            task_queue.pop();
        }

        queue_mutex_locker.mutex_unlock();
        return task;
    }

    template<class T>
    void *threadpool<T>::worker(void *arg) {
        auto *pool = reinterpret_cast<threadpool *> (arg);
        pool->run();
        return pool;
    }

    template<class T>
    void threadpool<T>::run() {
        while (!is_stop) {
            T* task = getTask();
            if (task == nullptr)
                queue_cond_locker.wait();
            else {
                try {
                    task->doit();
                }
                catch (std::exception &e) {
                    std::cout << "ERROR: " << e.what() << "\n";
                    throw e;
                }
                delete task;
            }
        }
    }

    template class threadpool<task>;
}