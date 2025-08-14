#include <csignal>
#include <cassert>
#include <future>

#include "ManapiThreadPool.hpp"
#include "ManapiAsync.hpp"
#include "ManapiDebug.hpp"
#include "./include/ManapiUtils.hpp"


void task_doit(std::move_only_function<void()> task, manapi::logger *logger) {
    assert((task));
    try {
        task();
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

void task_doit(manapi::move_only_function<void()> task, manapi::logger *logger) {
    assert((task));

    try {
        task();
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
    mthreadpool::mthreadpool(std::shared_ptr<manapi::logger> logger, ssize_t thread_num): threadpool(std::move(logger)), flags(0) {
        this->threadnum = thread_num;
        this->tasks_by_thread.resize(this->threadnum);
    }

    
    mthreadpool::~mthreadpool() = default;

    
    void mthreadpool::resize(ssize_t thread_num) {
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

    
    void mthreadpool::stop() MANAPIHTTP_NOEXCEPT {
        if (!(this->flags & 0b1)) {
            return;
        }
        this->flags ^= 0b1;
        this->cv.notify_all();
    }

    
    std::size_t mthreadpool::size() const {
        return this->threadnum;
    }

    
    void mthreadpool::clear() {
        if (!(this->flags & 0b1)) {
            this->tasks.clear();
        }
    }

    
    void mthreadpool::join() MANAPIHTTP_NOEXCEPT {
        for (auto &thread: this->threads) {
            thread.join();
        }

        this->threads.clear();
    }

    
    void mthreadpool::for_all_threads(std::move_only_function<void(tasks_by_thread_t *)> cb) {
        {
            std::lock_guard<std::mutex> lk (this->queue_mutex);
            cb (&this->tasks_by_thread);
        }

        this->cv.notify_all();
    }

    
    void mthreadpool::start() {
        if (this->flags & 0b1) {
            return;
        }

        this->flags |= 0b1;

        for (ssize_t i = 0; i < this->threadnum; ++i) {
            this->threads.emplace_back(mthreadpool::worker, this, i);
        }
    }

    
    void mthreadpool::append_task(std::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT {
        {
            // obtain a mutex
            std::lock_guard<std::mutex> lk (this->queue_mutex);

            MANAPIHTTP_MUST_ALLOC_START
            this->tasks.emplace_back(nullptr);
            MANAPIHTTP_MUST_ALLOC_END
            *this->tasks.rbegin() = std::move(cb);
        }

        this->cv.notify_one();
    }

    void mthreadpool::append_super_task(manapi::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT {
        {
            // obtain a mutex
            std::lock_guard<std::mutex> lk (this->queue_mutex);

            MANAPIHTTP_MUST_ALLOC_START
            this->tasks2.emplace_back(nullptr);
            MANAPIHTTP_MUST_ALLOC_END
            *this->tasks2.rbegin() = std::move(cb);
        }

        this->cv.notify_one();
    }


    int mthreadpool::get_task(ssize_t index, std::move_only_function<void()> *cb1, manapi::move_only_function<void()> *cb2) {
        std::lock_guard<std::mutex> lk (this->queue_mutex);

        if (index == -1 || this->tasks_by_thread[index].empty()) {
            // from n ... 0 by level

            if (!this->tasks.empty())
            {
                *cb1 = std::move(*this->tasks.rbegin());
                this->tasks.pop_back ();
                return 1;
            }
        }
        else {
            *cb1 = std::move(this->tasks_by_thread[index].back());
            this->tasks_by_thread[index].pop_back();
            return 1;
        }

        if (!this->tasks2.empty()) {
            *cb2 = std::move(*this->tasks2.rbegin());
            this->tasks2.pop_back ();
            return 2;
        }

        return 0;
    }


    
    void *mthreadpool::worker(void *arg, ssize_t index) {
        auto *pool = static_cast<mthreadpool *> (arg);
        pool->run(index);
        return pool;
    }

    
    void mthreadpool::run(ssize_t index) {
        std::move_only_function<void()> cb1;
        manapi::move_only_function<void()> cb2;

        while ((this->flags & 0b1)) {
            switch (get_task(index, &cb1, &cb2)) {
                case 0: {
                    std::unique_lock<std::mutex> lk (this->m);
                    this->cv.wait(lk);
                    break;
                }
                case 1:
                    manapi::async::internal::current_stack_cnt_set(0);
                    task_doit(std::move(cb1), this->logger_.get());
                break;
                case 2:
                    manapi::async::internal::current_stack_cnt_set(0);
                    task_doit(std::move(cb2), this->logger_.get());
                break;
            }
        }
    }

    
    ethreadpool::ethreadpool(std::shared_ptr<manapi::logger> logger, std::move_only_function<void()> ontask) : threadpool(std::move(logger)) {
        this->flags_ = 0;
        this->ontask_ = std::move(ontask);
    }

    
    ethreadpool::~ethreadpool() = default;

    
    bool ethreadpool::try_task() {
        if (!this->tasks.empty()) {
            auto task = std::move(this->tasks.front());
            this->tasks.pop_front();

            manapi::async::internal::current_stack_cnt_set(0);
            task_doit(std::move(task), this->logger_.get());

            return true;
        }

        if(!this->tasks2.empty()) {
            auto task = std::move(this->tasks2.front());
            this->tasks2.pop_front();

            manapi::async::internal::current_stack_cnt_set(0);
            task_doit(std::move(task), this->logger_.get());

            return true;
        }

        return false;
    }

    
    void ethreadpool::set_notify() MANAPIHTTP_NOEXCEPT {
        this->flags_ |= 0b10;
    }

    
    void ethreadpool::set_notify_cb(std::move_only_function<void()> ontask) MANAPIHTTP_NOEXCEPT {
        this->ontask_ = std::move(ontask);
    }

    
    void ethreadpool::stop() MANAPIHTTP_NOEXCEPT {
        if (!(this->flags_ & 0b1)) {
            return;
        }

        this->flags_ ^= 0b1;
    }

    
    void ethreadpool::start() {
        if (this->flags_ & 0b1) {
            return;
        }

        this->flags_ |= 0b1;
    }


    void ethreadpool::append_task(std::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT {
        MANAPIHTTP_MUST_ALLOC_START
        this->tasks.emplace_back(nullptr);
        MANAPIHTTP_MUST_ALLOC_END
        (*this->tasks.rbegin()) = std::move(cb);

        if ((this->flags_ & 0b10) && this->ontask_) {
            try {
                this->flags_ ^= 0b10;
                this->ontask_();
            }
            catch (std::exception const &e) {
                manapi_log_error("%s failed due to %s", "event thread pool: ontask()", e.what());
            }
        }
    }

    void ethreadpool::append_super_task(manapi::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT {
        MANAPIHTTP_MUST_ALLOC_START
        this->tasks2.emplace_back(nullptr);
        MANAPIHTTP_MUST_ALLOC_END
        (*this->tasks2.rbegin()) = std::move(cb);

        if ((this->flags_ & 0b10) && this->ontask_) {
            try {
                this->flags_ ^= 0b10;
                this->ontask_();
            }
            catch (std::exception const &e) {
                manapi_log_error("%s failed due to %s", "event thread pool: ontask()", e.what());
            }
        }
    }


    void ethreadpool::join() MANAPIHTTP_NOEXCEPT {

    }
}