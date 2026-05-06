#include <csignal>
#include <cassert>
#include <future>

#include "ManapiThreadPool.hpp"
#include "ManapiAsync.hpp"
#include "ManapiDebug.hpp"
#include "./include/ManapiUtils.hpp"

struct manapi::mthreadpool::data_t {
    std::size_t m_threadnum;

    std::vector <std::thread> m_threads;

    std::mutex m_m;

    std::condition_variable m_cv;

    tasks_by_thread_t m_tasks_by_thread;

    std::vector <std::move_only_function<void()>> m_tasks;

    std::vector <manapi::fixed_function<void()>> m_tasks2;

    std::mutex m_queue_mutex;

    std::atomic<int> m_flags;
};

struct manapi::ethreadpool::data_t {
    int m_flags;

    std::vector <std::move_only_function<void()>> m_tasks;

    std::vector <manapi::fixed_function<void()>> m_tasks2;

    std::move_only_function<void()> m_ontask;

    std::shared_ptr<manapi::logger> m_logger;
};

static void task_doit(std::move_only_function<void()>&task, manapi::logger *logger) {
    assert((task));
    try {
        task();
    }
    catch (const manapi::exception &e) {
        if (logger)
            logger->fwarning( "unexpected exception in the task with error code %d: %s", static_cast<int>(e.err_num()), e.what());
        else
            manapi_log_warn("unexpected exception in the task with error code %d: %s", static_cast<int>(e.err_num()), e.what());
    }
    catch (const std::exception &e) {
        if (logger)
            logger->fwarning( "unexpected exception in the task: %s", e.what());
        else
            manapi_log_warn("unexpected exception in the task: %s", e.what());
    }
}

static void task_doit(manapi::fixed_function<void()> &task, manapi::logger *logger) {
    assert((task));

    try {
        task();
    }
    catch (const manapi::exception &e) {
        logger->warning(std::format("unexpected exception in the task with error code {}: {}", static_cast<int>(e.err_num()), e.what()));
    }
    catch (const std::exception &e) {
        logger->warning(std::format("unexpected exception in the task: {}", e.what()));
    }
}


static int mthreadpool_get_task(manapi::mthreadpool::data_t *data, std::size_t index, std::move_only_function<void()> *cb1, manapi::fixed_function<void()> *cb2) {
    std::lock_guard<std::mutex> lk (data->m_queue_mutex);

    if (data->m_tasks_by_thread[index].empty()) {
        if (!data->m_tasks.empty())
        {
            *cb1 = std::move(data->m_tasks.back());
            data->m_tasks.pop_back ();
            return 1;
        }
    }
    else {
        *cb1 = std::move(data->m_tasks_by_thread[index].back());
        data->m_tasks_by_thread[index].pop_back();
        return 1;
    }

    if (!data->m_tasks2.empty()) {
        *cb2 = std::move(data->m_tasks2.back());
        data->m_tasks2.pop_back ();
        return 2;
    }

    return 0;
}

static void mthreadpool_run(manapi::mthreadpool::data_t *data, std::size_t index) {
    std::move_only_function<void()> cb1;
    manapi::fixed_function <void()> cb2;

    while ((data->m_flags & 0b1)) {
        switch (mthreadpool_get_task(data, index, &cb1, &cb2)) {
            case 0: {
                std::unique_lock<std::mutex> lk (data->m_m);
                data->m_cv.wait(lk);
                break;
            }
            case 1:
                manapi::async::internal::current_stack_cnt_set(0);
                task_doit(cb1, nullptr);
            break;
            case 2:
                manapi::async::internal::current_stack_cnt_set(0);
                task_doit(cb2, nullptr);
            break;
        }
    }
}



static void *mthreadpool_worker(void *arg, std::size_t index) {
    auto *pool = static_cast<manapi::mthreadpool::data_t *> (arg);
    mthreadpool_run(pool, index);
    return pool;
}


namespace manapi {
    mthreadpool::mthreadpool(std::size_t thread_num) {
        this->m_data = std::make_unique<data_t>();
        this->m_data->m_flags = 0;
        this->m_data->m_threadnum = thread_num;
        this->m_data->m_threadnum = thread_num;
        this->m_data->m_tasks_by_thread.resize(this->m_data->m_threadnum);
        this->m_data->m_tasks2.reserve(512);
        this->m_data->m_tasks.reserve(512);
    }

    
    mthreadpool::~mthreadpool() = default;

    
    void mthreadpool::resize(std::size_t thread_num) {
        if (!(this->m_data->m_flags & 0b1)) {
            this->m_data->m_threadnum = thread_num;
            this->m_data->m_tasks_by_thread.resize(thread_num);
        }
        else {
            this->stop();
            this->join();
            this->start();
        }
    }

    
    void mthreadpool::stop() MANAPIHTTP_NOEXCEPT {
        if (!(this->m_data->m_flags & 0b1)) {
            return;
        }
        this->m_data->m_flags ^= 0b1;
        this->m_data->m_cv.notify_all();
    }

    
    std::size_t mthreadpool::size() const MANAPIHTTP_NOEXCEPT {
        return this->m_data->m_threadnum;
    }

    std::size_t mthreadpool::tasks_size() const MANAPIHTTP_NOEXCEPT {
        std::lock_guard<std::mutex> lk (this->m_data->m_queue_mutex);
        return this->m_data->m_tasks.size() + this->m_data->m_tasks2.size();
    }

    void mthreadpool::clear() {
        if (!(this->m_data->m_flags & 0b1)) {
            this->m_data->m_tasks.clear();
            this->m_data->m_tasks2.clear();
        }
    }

    
    void mthreadpool::join() MANAPIHTTP_NOEXCEPT {
        for (auto &thread: this->m_data->m_threads) {
            thread.join();
        }

        this->m_data->m_threads.clear();
    }

    
    void mthreadpool::for_all_threads(std::move_only_function<void(tasks_by_thread_t *)> cb) {
        {
            std::lock_guard<std::mutex> lk (this->m_data->m_queue_mutex);
            cb (&this->m_data->m_tasks_by_thread);
        }

        this->m_data->m_cv.notify_all();
    }

    
    void mthreadpool::start() {
        if (this->m_data->m_flags & 0b1) {
            return;
        }

        this->m_data->m_flags |= 0b1;

        for (std::size_t i = 0; i < this->m_data->m_threadnum; ++i) {
            this->m_data->m_threads.emplace_back(mthreadpool_worker, this->m_data.get(), i);
        }
    }

    
    void mthreadpool::append_task(std::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT {

        {
            // obtain a mutex
            std::lock_guard<std::mutex> lk (this->m_data->m_queue_mutex);

            MANAPIHTTP_MUST_ALLOC_START
            this->m_data->m_tasks.emplace_back(nullptr);
            MANAPIHTTP_MUST_ALLOC_END
        assert(cb);
            this->m_data->m_tasks.back() = std::move(cb);
        }

        this->m_data->m_cv.notify_one();
    }

    void mthreadpool::append_static_task(manapi::fixed_function<void()> cb) MANAPIHTTP_NOEXCEPT {
        {
            // obtain a mutex
            std::lock_guard<std::mutex> lk (this->m_data->m_queue_mutex);

            MANAPIHTTP_MUST_ALLOC_START
            this->m_data->m_tasks2.emplace_back(nullptr);
            MANAPIHTTP_MUST_ALLOC_END
            this->m_data->m_tasks2.back() = std::move(cb);
        }

        this->m_data->m_cv.notify_one();
    }

    ethreadpool::ethreadpool(std::shared_ptr<manapi::logger> logger, std::move_only_function<void()> ontask) {
        this->m_data = std::make_unique<data_t>();
        this->m_data->m_logger = std::move(logger);
        this->m_data->m_flags = 0;
        this->m_data->m_ontask = std::move(ontask);
        this->m_data->m_tasks2.reserve(1024);
        this->m_data->m_tasks.reserve(1024);
    }

    
    ethreadpool::~ethreadpool() = default;

    std::size_t ethreadpool::tasks_size() const MANAPIHTTP_NOEXCEPT {
        return this->m_data->m_tasks.size() + this->m_data->m_tasks2.size();
    }


    bool ethreadpool::try_task() {
        if (!this->m_data->m_tasks.empty()) {
            auto task = std::move(this->m_data->m_tasks.back());
            this->m_data->m_tasks.pop_back();

            manapi::async::internal::current_stack_cnt_set(0);
            task_doit(task, this->m_data->m_logger.get());

            return true;
        }

        if(!this->m_data->m_tasks2.empty()) {
            auto task = std::move(this->m_data->m_tasks2.back());
            this->m_data->m_tasks2.pop_back();

            manapi::async::internal::current_stack_cnt_set(0);
            task_doit(task, this->m_data->m_logger.get());

            return true;
        }

        return false;
    }

    
    void ethreadpool::set_notify() MANAPIHTTP_NOEXCEPT {
        this->m_data->m_flags |= 0b10;
    }

    
    void ethreadpool::set_notify_cb(std::move_only_function<void()> ontask) MANAPIHTTP_NOEXCEPT {
        this->m_data->m_ontask = std::move(ontask);
    }

    
    void ethreadpool::stop() MANAPIHTTP_NOEXCEPT {
        if (!(this->m_data->m_flags & 0b1)) {
            return;
        }

        this->m_data->m_flags ^= 0b1;
    }

    
    void ethreadpool::start() {
        if (this->m_data->m_flags & 0b1) {
            return;
        }

        this->m_data->m_flags |= 0b1;
    }


    void ethreadpool::append_task(std::move_only_function<void()> cb) MANAPIHTTP_NOEXCEPT {

        MANAPIHTTP_MUST_ALLOC_START
        this->m_data->m_tasks.emplace_back(nullptr);
        MANAPIHTTP_MUST_ALLOC_END
        assert(cb);
        this->m_data->m_tasks.back() = std::move(cb);

        if ((this->m_data->m_flags & 0b10) && this->m_data->m_ontask) {
            try {
                this->m_data->m_flags ^= 0b10;
                this->m_data->m_ontask();
            }
            catch (std::exception const &e) {
                manapi_log_error("%s failed due to %s", "event thread pool: ontask()", e.what());
            }
        }
    }

    void ethreadpool::append_static_task(manapi::fixed_function<void()> cb) MANAPIHTTP_NOEXCEPT {
        MANAPIHTTP_MUST_ALLOC_START
        this->m_data->m_tasks2.emplace_back(nullptr);
        MANAPIHTTP_MUST_ALLOC_END
        this->m_data->m_tasks2.back() = std::move(cb);

        if ((this->m_data->m_flags & 0b10) && this->m_data->m_ontask) {
            try {
                this->m_data->m_flags ^= 0b10;
                this->m_data->m_ontask();
            }
            catch (std::exception const &e) {
                manapi_log_error("%s failed due to %s", "event thread pool: ontask()", e.what());
            }
        }
    }


    void ethreadpool::join() MANAPIHTTP_NOEXCEPT {

    }

    const std::shared_ptr<manapi::logger> & ethreadpool::logger() MANAPIHTTP_NOEXCEPT {
        return this->m_data->m_logger;
    }
}
