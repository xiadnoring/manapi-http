#include <csignal>

#include "services/ManapiThreadPool.hpp"

#include <future>

#include "services/ManapiTask.hpp"
#include "services/ManapiTaskFunction.hpp"
#include "ManapiUtils.hpp"

namespace manapi::net {
    template<class T>
    threadpool<T>::threadpool(size_t thread_num, size_t queues_count): thread_num(thread_num),is_stop(true),stopped(0) {
        sigemptyset(&this->blockedSignal);
        sigaddset(&this->blockedSignal, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &this->blockedSignal, nullptr);

        this->task_queues.resize(queues_count);
    }

    template<class T>
    threadpool<T>::~threadpool() {
        stop();
    }

    template<class T>
    void threadpool<T>::resize(size_t thread_num) {
        if (this->is_stop) {
            this->thread_num = thread_num;
        }
    }

    template<class T>
    void threadpool<T>::stop() {
        if (this->is_stop) {
            return;
        }
        this->is_stop.store(true);
        this->cv.notify_all();
    }

    template<class T>
    void threadpool<T>::wait_stop() {
        std::unique_lock<std::mutex> lk (this->m);
        this->cv.wait (lk, [this] () -> bool { return this->stopped.load() >= this->thread_num; });
    }

    template<class T>
    void threadpool<T>::start() {
        if (!this->is_stop) {
            return;
        }
        this->is_stop.store(false);

        if (this->thread_num <= 0) {
            THROW_MANAPIHTTP_EXCEPTION(ERR_CONFIG_ERROR, "threadpool can't be init because thread_number = {}", 0);
        }

        for (size_t i = this->all_threads.size(); i < this->thread_num; i++) {
            this->all_threads.emplace_back(worker, this);
            this->all_threads[i].detach();
        }
    }

    template<class T>
    bool threadpool<T>::append_task(std::unique_ptr<T> task, int level) {
        if (this->is_stop)
        {
            return false;
        }

        if (task == nullptr) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "Task is NULL");
        }

        // obtain a mutex
        this->queue_mutex.lock();

        // add into the queue
        this->task_queues[level].push_front (std::move(task));

        this->queue_mutex.unlock();

        // wake up the thread waiting for the task

        this->cv.notify_one();

        return true;
    }

    template<class T>
    void threadpool<T>::append_task(T task) {
        this->append_task(std::make_unique<T>(std::move(task)));
    }

    template<class T>
    void threadpool<T>::append_task(const std::function<void()> &cb) {
        this->append_task(std::make_unique<function_task>(cb));
    }

    template<class T>
    std::unique_ptr<T> threadpool<T>::get_task() {
        std::unique_ptr<T> task = nullptr;
        std::lock_guard<std::mutex> lk (this->queue_mutex);
        // from n ... 0 by level
        for (auto task_queue = this->task_queues.rbegin(); task_queue != this->task_queues.rend(); ++task_queue)
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
        while (!this->is_stop) {
            auto task = get_task();
            if (task == nullptr)
            {
                std::unique_lock<std::mutex> lk (this->m);
                this->cv.wait(lk);
            }
            else
            {
                task_doit(std::move(task));
            }
        }

        this->stopped.fetch_add(1);
        this->cv.notify_all();
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