#include "ManapiThreadPool.h"
#include "ManapiTask.h"
#include "ManapiUtils.h"

namespace manapi::net {
    template<class T>
    threadpool<T>::threadpool(size_t thread_num): thread_number(thread_num),is_stop(false),all_threads(nullptr),stopped(0) {
        if (thread_num <= 0)
            printf("threadpool cant init because thread_number = 0");

        all_threads = new pthread_t[thread_number];
        if (all_threads == nullptr)
        {
            THROW_MANAPI_EXCEPTION("cant init threadpool because thread array cant new: {}", "all_threads = nullptr")
        }
    }

    template<class T>
    bool threadpool<T>::all_tasks_stopped() {
        return get_count_stopped_task() >= thread_number;
    }

    template<class T>
    threadpool<T>::~threadpool() {
        delete []all_threads;
        stop();
    }

    template<class T>
    size_t threadpool<T>::get_count_stopped_task() {
        return stopped;
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
                THROW_MANAPI_EXCEPTION("{}", "failed to create thread pool");
            }
        }
    }

    template<class T>
    bool threadpool<T>::append_task(T *task) {
        if (is_stop)
        {
            return false;
        }

        // obtain a mutex
        queue_mutex_locker.mutex_lock();
        bool is_signal = task_queue.empty();
        // add into the queue
        task_queue.push(task);
        queue_mutex_locker.mutex_unlock();

        // wake up the thread waiting for the task
        if (is_signal)
        {
            queue_cond_locker.signal();
        }
        return true;
    }

    template<class T>
    T* threadpool<T>::getTask() {
        T *task = nullptr;
        queue_mutex_locker.mutex_lock();
        if (!task_queue.empty())
        {
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
            {
                queue_cond_locker.wait();
            }
            else
            {
                try
                {
                    task->doit();
                }
                catch (const manapi::utils::exception &e) {
                    MANAPI_LOG ("Task Exception: ", e.what());
                }
                catch (const std::exception &e) {
                    MANAPI_LOG ("Task Exception: ", e.what());
                }

                if (task->to_delete)
                {
                    delete task;
                }
            }
        }

        stopped++;
    }

    template class threadpool<task>;
}