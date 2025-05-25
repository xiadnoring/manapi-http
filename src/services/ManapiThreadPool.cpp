#include <csignal>

#include "services/ManapiThreadPool.hpp"

#include <cassert>
#include <future>
#include <stacktrace>

#include "ManapiDebug.hpp"
#include "services/ManapiTask.hpp"
#include "services/ManapiTaskFunction.hpp"
#include "ManapiUtils.hpp"

template<class T>
void task_doit(std::unique_ptr<T> task, manapi::logger *logger) {
    assert((task.get()));
    try {
        task->doit();
    }
    catch (const manapi::exception &e) {
        logger->warning(manapi::logger::default_service,
            std::format("unexpected exception in the task with error code {}: {}", static_cast<int>(e.err_num()), e.what()));
    }
    catch (const std::exception &e) {
        logger->warning(manapi::logger::default_service,
            std::format("unexpected exception in the task: {}", e.what()));
    }
}

namespace manapi {
    template<class T>
    mthreadpool<T>::mthreadpool(std::shared_ptr<manapi::logger> logger, ssize_t thread_num): threadpool<T>(std::move(logger)), flags(0) {
        this->threadnum = thread_num;
        this->tasks_by_thread.resize(this->threadnum);
    }

    template<class T>
    mthreadpool<T>::~mthreadpool() {

    }

    template<class T>
    void mthreadpool<T>::resize(ssize_t thread_num) {
        if (!(this->flags & 0b1)) {
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
    void mthreadpool<T>::stop() {
        if (!(this->flags & 0b1)) {
            return;
        }
        this->flags ^= 0b1;
        this->cv.notify_all();
    }

    template<class T>
    std::size_t mthreadpool<T>::size() const {
        return this->threadnum;
    }

    template<class T>
    void mthreadpool<T>::clear() {
        if (!(this->flags & 0b1)) {
            this->tasks.clear();
        }
    }

    template<class T>
    void mthreadpool<T>::join() {
        for (auto &thread: this->threads) {
            thread.join();
        }

        this->threads.clear();
    }

    template<class T>
    void mthreadpool<T>::for_all_threads(std::move_only_function<void(tasks_by_thread_t *)> cb) {
        {
            std::lock_guard<std::mutex> lk (this->queue_mutex);
            cb (&this->tasks_by_thread);
        }

        this->cv.notify_all();
    }

    template<class T>
    void mthreadpool<T>::start() {
        if (this->flags & 0b1) {
            return;
        }

        this->flags |= 0b1;

        for (ssize_t i = 0; i < this->threadnum; ++i) {
            this->threads.push_back(std::thread(mthreadpool::worker, this, i));
        }
    }

    template<class T>
    void mthreadpool<T>::append_task(std::unique_ptr<T> task) {
        {
            // obtain a mutex
            std::lock_guard<std::mutex> lk (this->queue_mutex);

            try {
                // add into the queue
                this->tasks.push_back (std::move(task));
            }
            catch (...) {
                std::cout << std::stacktrace::current() << "\n";
            }
            // wake up the thread waiting for the task
        }

        this->cv.notify_one();
    }

    template<class T>
    void mthreadpool<T>::append_task(T task) {
        this->append_task(std::make_unique<T>(std::move(task)));
    }

    template<class T>
    void mthreadpool<T>::append_task(std::move_only_function<void()> cb) {
        this->append_task(std::make_unique<function_task>(std::move(cb)));
    }

    template<class T>
    std::unique_ptr<T> mthreadpool<T>::get_task(ssize_t index) {
        std::unique_ptr<T> task = nullptr;
        std::lock_guard<std::mutex> lk (this->queue_mutex);

        if (index == -1 || this->tasks_by_thread[index].empty()) {
            // from n ... 0 by level

            if (!this->tasks.empty())
            {
                task = std::move(*this->tasks.rbegin());
                this->tasks.pop_back ();
            }
        }
        else {
            task = std::move(this->tasks_by_thread[index].back());
            this->tasks_by_thread[index].pop_back();
        }

        return std::move(task);
    }


    template<class T>
    void *mthreadpool<T>::worker(void *arg, ssize_t index) {
        auto *pool = static_cast<mthreadpool *> (arg);
        pool->run(index);
        return pool;
    }

    template<class T>
    void mthreadpool<T>::run(ssize_t index) {
        while ((this->flags & 0b1)) {
            auto task = get_task(index);
            if (task == nullptr)
            {
                std::unique_lock<std::mutex> lk (this->m);
                this->cv.wait(lk);
            }
            else
            {
                task_doit(std::move(task), this->logger_.get());
            }
        }
    }

    template<class T>
    ethreadpool<T>::ethreadpool(std::shared_ptr<manapi::logger> logger, std::move_only_function<void()> ontask) : threadpool<T>(std::move(logger)) {
        this->flags_ = 0;
        this->ontask_ = std::move(ontask);
    }

    template<class T>
    ethreadpool<T>::~ethreadpool() = default;

    template<class T>
    bool ethreadpool<T>::try_task() {
        if (this->tasks.empty()) {
            return false;
        }

        auto task = std::move(this->tasks.front());
        this->tasks.pop_front();

        task_doit(std::move(task), this->logger_.get());
        return true;
    }

    template<class T>
    void ethreadpool<T>::set_notify() {
        this->flags_ |= 0b10;
    }

    template<class T>
    void ethreadpool<T>::set_notify_cb(std::move_only_function<void()> ontask) {
        this->ontask_ = std::move(ontask);
    }

    template<class T>
    void ethreadpool<T>::stop() {
        if (!(this->flags_ & 0b1)) {
            return;
        }

        this->flags_ ^= 0b1;
    }

    template<class T>
    void ethreadpool<T>::start() {
        if (this->flags_ & 0b1) {
            return;
        }

        this->flags_ |= 0b1;
    }

    template<class T>
    void ethreadpool<T>::append_task(std::unique_ptr<T> task) {
        this->tasks.push_back(std::move(task));

        if ((this->flags_ & 0b10) && this->ontask_) {
            this->flags_ ^= 0b10;
            this->ontask_();
        }
    }

    template<class T>
    void ethreadpool<T>::append_task(T task) {
        this->append_task(std::make_unique<T>(std::move(task)));
    }

    template<class T>
    void ethreadpool<T>::append_task(std::move_only_function<void()> cb) {
        this->append_task(std::make_unique<function_task>(std::move(cb)));
    }

    template<class T>
    void ethreadpool<T>::join() {

    }


    template class mthreadpool<task>;
    template class ethreadpool<task>;
}