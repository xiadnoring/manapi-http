#include <csignal>

#include "services/ManapiThreadPool.hpp"

#include <future>

#include "services/ManapiTask.hpp"
#include "services/ManapiTaskFunction.hpp"
#include "ManapiUtils.hpp"

namespace manapi::net {
    template<class T>
    threadpool<T>::threadpool(size_t thread_num, size_t queues_count): thread_number(thread_num),is_stop(true),stopped(0UL) {
        sigemptyset(&blockedSignal);
        sigaddset(&blockedSignal, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &blockedSignal, nullptr);

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
    void threadpool<T>::resize(size_t thread_num) {
        if (is_stop) {
            this->thread_number = thread_num;
        }
    }

    template<class T>
    size_t threadpool<T>::get_count_stopped_task() {
        return *stopped.get();
    }

    template<class T>
    void threadpool<T>::stop() {
        if (is_stop) {
            return;
        }
        is_stop.store(true);
        cv.notify_all();
    }

    template<class T>
    void threadpool<T>::start() {
        if (!is_stop) {
            return;
        }
        is_stop.store(false);

        if (this->thread_number <= 0) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "threadpool cant init because thread_number = {}", 0);
        }

        for (size_t i = all_threads.size(); i < thread_number; i++) { all_threads.emplace_back(worker, this); all_threads[i].detach(); }
    }

    template<class T>
    bool threadpool<T>::append_task(std::unique_ptr<T> task, int level) {
        if (is_stop)
        {
            return false;
        }

        // obtain a mutex
        queue_mutex.lock();

        // add into the queue
        task_queues[level].push_front (std::move(task));

        queue_mutex.unlock();

        // wake up the thread waiting for the task

        cv.notify_one();

        return true;
    }

    template<class T>
    void threadpool<T>::append_task(T task) {
        this->append_task(std::make_unique<T>(std::move(task)));
    }

    template<class T>
    void threadpool<T>::append_task(std::coroutine_handle<> handle) {
        this->append_task(std::move(manapi::net::function_task ([handle] () -> void {
            handle ();
        })));
    }

    template<class T>
    void threadpool<T>::append_task(const std::function<void()> &cb) {
        this->append_task(std::make_unique<function_task>(cb));
    }

    template<class T>
    std::unique_ptr<T> threadpool<T>::get_task() {
        std::unique_ptr<T> task = nullptr;
        std::lock_guard<std::mutex> lk (queue_mutex);
        // from n ... 0 by level
        for (auto task_queue = task_queues.rbegin(); task_queue != task_queues.rend(); task_queue++)
        {
            if (!task_queue->empty())
            {
                task = std::move(task_queue->back());
                task_queue->pop_back();
                break;
            }
        }

        return std::move(task);
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
            auto task = get_task();
            if (task == nullptr)
            {
                std::unique_lock<std::mutex> lk (m);
                cv.wait(lk);
            }
            else
            {
                task_doit(std::move(task));
            }
        }

        ++stopped;
    }

    template<class T>
    void threadpool<T>::task_doit(std::unique_ptr<T> task) {
        try
        {
            task->doit();
        }
        catch (const manapi::net::utils::exception &e) {
            MANAPIHTTP_LOG ("Task Manapi Exception: {}", e.what());
        }
        // catch (const std::exception &e) {
        //     MANAPIHTTP_LOG ("Task Default Exception: {}", e.what());
        // }
    }

    template class threadpool<task>;
}