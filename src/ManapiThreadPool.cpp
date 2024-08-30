#include <csignal>

#include "ManapiThreadPool.h"

#include <future>

#include "ManapiTask.h"
#include "ManapiUtils.h"

namespace manapi::net {
    template<class T>
    threadpool<T>::threadpool(size_t thread_num, size_t queues_count): thread_number(thread_num),is_stop(false),stopped(0) {
        sigemptyset(&blockedSignal);
        sigaddset(&blockedSignal, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &blockedSignal, nullptr);

        if (thread_num <= 0)
        {
            THROW_MANAPI_EXCEPTION("threadpool cant init because thread_number = {}", 0);
        }

        if (queues_count <= 0)
        {
            THROW_MANAPI_EXCEPTION("{} < 0 in threadpool", "queues_count");
        }

        task_queues.resize(queues_count);
    }

    template<class T>
    bool threadpool<T>::all_tasks_stopped() {
        return get_count_stopped_task() >= thread_number;
    }

    template<class T>
    threadpool<T>::~threadpool() {
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
        for (size_t i = all_threads.size(); i < thread_number; i++) { all_threads.emplace_back(worker, this); all_threads[i].detach(); }
    }

    template<class T>
    bool threadpool<T>::append_task(T *task, int level) {
        if (is_stop)
        {
            return false;
        }

        // obtain a mutex
        queue_mutex.lock();


        if (level >= task_queues.size())
        {
            queue_mutex.unlock();

            std::thread t ([this, task] () -> void { task_doit(task); });
            t.detach();
        }
        else
        {
            // add into the queue
            task_queues[level].push(task);

            queue_mutex.unlock();

            // wake up the thread waiting for the task

            cv.notify_one();
        }

        return true;
    }

    template<class T>
    T* threadpool<T>::getTask() {
        T *task = nullptr;
        std::lock_guard<std::mutex> lk (queue_mutex);
        // from n ... 0 by level
        for (auto task_queue = task_queues.rbegin(); task_queue != task_queues.rend(); task_queue++)
        {
            if (!task_queue->empty())
            {
                task = task_queue->front();
                task_queue->pop();
                break;
            }
        }

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
                // RESET
                task->to_retry = false;
                task->to_delete = true;

                append_task(task);
            }
        }
    }

    template class threadpool<task>;
}