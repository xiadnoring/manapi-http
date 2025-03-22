#include <csignal>

#include "services/ManapiThreadPool.hpp"

#include <future>

#include "services/ManapiTask.hpp"
#include "services/ManapiTaskFunction.hpp"
#include "ManapiUtils.hpp"

namespace manapi {
    template<class T>
    threadpool<T>::threadpool(ssize_t thread_num, ssize_t queues_count): is_stop(true) {
#if defined(__unix__)||defined(__APPLE__)
        sigemptyset(&this->blockedSignal);
        sigaddset(&this->blockedSignal, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &this->blockedSignal, nullptr);
#endif
        this->task_queues.resize(queues_count);
        this->threadnum = thread_num;
        this->tasks_by_thread.resize(this->threadnum);
    }

    template<class T>
    threadpool<T>::~threadpool() {
        this->stop();
        this->join();
    }

    template<class T>
    void threadpool<T>::resize(ssize_t thread_num) {
        if (this->is_stop) {
            this->threadnum = thread_num;
            this->tasks_by_thread.resize(thread_num);
        }
        else {
            this->stop();
            this->join();
            this->start();
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
    std::size_t threadpool<T>::size() const {
        return this->threadnum;
    }

    template<class T>
    void threadpool<T>::clear() {
        if (this->is_stop) {
            for (auto &queue: this->task_queues) {
                queue.clear();
            }
        }
    }

    template<class T>
    void threadpool<T>::join() {
        for (auto &thread: this->threads) {
            thread.join();
        }

        this->threads.clear();
    }

    template<class T>
    void threadpool<T>::for_all_threads(std::function<void()> cb) {
        {
            std::lock_guard<std::mutex> lk (this->queue_mutex);
            for (auto &row : this->tasks_by_thread) {
                row.push_back(std::make_unique<net::function_task>(cb));
            }
        }

        this->cv.notify_all();
    }

    template<class T>
    bool threadpool<T>::try_todo_task() {
        if (!this->is_stop) {
            auto task = get_task(-1);
            if (task) {
                task_doit(std::move(task));
                return true;
            }
        }
        return false;
    }

    template<class T>
    void threadpool<T>::start() {
        if (!this->is_stop) {
            return;
        }

        this->is_stop.store(false);

        for (ssize_t i = 0; i < this->threadnum; ++i) {
            this->threads.push_back(std::thread(threadpool::worker, this, i));
        }
    }

    template<class T>
    bool threadpool<T>::append_task(std::unique_ptr<T> task, int level) {
        if (!task) {
            return false;
        }

        // obtain a mutex
        this->queue_mutex.lock();

        // add into the queue
        this->task_queues[level].push_back (std::move(task));

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
    void threadpool<T>::append_task(std::move_only_function<void()> cb) {
        this->append_task(std::make_unique<net::function_task>(std::move(cb)));
    }

    template<class T>
    std::unique_ptr<T> threadpool<T>::get_task(ssize_t index) {
        std::unique_ptr<T> task = nullptr;
        std::lock_guard<std::mutex> lk (this->queue_mutex);

        if (index == -1 || this->tasks_by_thread[index].empty()) {
            // from n ... 0 by level
            for (auto task_queue = this->task_queues.rbegin(); task_queue != this->task_queues.rend(); ++task_queue)
            {
                if (!task_queue->empty())
                {
                    task = std::move(*task_queue->rbegin());
                    task_queue->pop_back ();
                    break;
                }
            }
        }
        else {
            task = std::move(this->tasks_by_thread[index].back());
            this->tasks_by_thread[index].pop_back();
        }

        return std::move(task);
    }

    template<class T>
    void *threadpool<T>::worker(void *arg, ssize_t index) {
        auto *pool = static_cast<threadpool *> (arg);
        pool->run(index);
        return pool;
    }

    template<class T>
    void threadpool<T>::run(ssize_t index) {
        while (!this->is_stop) {
            auto task = get_task(index);
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
    }

    template<class T>
    void threadpool<T>::task_doit(std::unique_ptr<T> task) {
        try
        {
            task->doit();
        }
        catch (const manapi::exception &e) {
            MANAPIHTTP_LOG ("Task Manapi Exception: {}", e.what());
        }
        // catch (const std::exception &e) {
        //     MANAPIHTTP_LOG ("Task Default Exception: {}", e.what());
        // }
    }

    template class threadpool<task>;
}