#include <csignal>

#include "ManapiThreadPool.h"
#include "ManapiTask.h"
#include "ManapiUtils.h"

namespace manapi::net {
    template<class T>
    threadpool<T>::threadpool(size_t thread_num): thread_number(thread_num),is_stop(false),all_threads(nullptr),stopped(0) {
        sigemptyset(&blockedSignal);
        sigaddset(&blockedSignal, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &blockedSignal, nullptr);

        if (thread_num <= 0)
        {
            THROW_MANAPI_EXCEPTION("threadpool cant init because thread_number = {}", 0);
        }

        all_threads = new pthread_t[thread_number];
        if (all_threads == nullptr)
        {
            THROW_MANAPI_EXCEPTION("cant init threadpool because thread array cant new: {}", "all_threads = nullptr");
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
        cv.notify_all();
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
    bool threadpool<T>::append_task(T *task, bool important) {
        if (is_stop)
        {
            return false;
        }

        // obtain a mutex
        queue_mutex_locker.mutex_lock();


        if (important && !task_queue.empty())
        {
            queue_mutex_locker.mutex_unlock();

            std::thread t ([this, task] () -> void { task_doit(task); });
            t.detach();
        }
        else
        {
            // add into the queue
            task_queue.push(task);

            queue_mutex_locker.mutex_unlock();

            // wake up the thread waiting for the task

            cv.notify_one();
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
        auto *pool = static_cast<threadpool *> (arg);
        pool->run();
        return pool;
    }

    template<class T>
    void threadpool<T>::run() {
        while (!is_stop) {
            T* task = getTask();
            if (task == nullptr)
            {
                std::unique_lock<std::mutex> lk (m);
                cv.wait(lk);
            }
            else
            {
                task_doit(task);
            }
        }

        stopped++;
    }

    template<class T>
    void threadpool<T>::task_doit(T *task) {
        try
        {
            task->doit();
        }
        catch (const manapi::utils::exception &e) {
            MANAPI_LOG ("Task Manapi Exception: {}", e.what());
        }
        catch (const std::exception &e) {
            MANAPI_LOG ("Task Default Exception: {}", e.what());
        }

        if (task->to_delete)
        {
            delete task;
        }
        else
        {
            if (task->to_retry)
            {
                queue_mutex_locker.mutex_lock();

                // RESET
                task->to_retry = false;
                task->to_delete = true;

                task_queue.push(task);

                queue_mutex_locker.mutex_unlock();
            }
        }
    }

    template class threadpool<task>;
}