#include <csignal>
#include <cassert>
#include <future>

#include "ManapiThreadPool.hpp"
#include "ManapiAsync.hpp"
#include "ManapiDebug.hpp"
#include "./include/ManapiUtils.hpp"

#define THREADPOOL__PREALLOC_ALIGN ((std::size_t)512)

enum threadpool__flags {
    THREADPOOL__FLAG_ACTIVE = 1<<0,
    THREADPOOL__FLAG_NOTIFY = 1<<1
};

struct manapi::mthreadpool::data_t {
    std::size_t m_threadnum;

    std::vector <std::thread> m_threads;

    std::mutex m_m;

    std::condition_variable m_cv;

    tasks_by_thread_t m_tasks_by_thread;

    std::vector <std::move_only_function<void()>> m_tasks;

    std::vector <std::coroutine_handle<>> m_handlers;

    std::mutex m_queue_mutex;

    std::atomic<int> m_flags;

    std::size_t m_reserved_tasks;

    std::size_t m_reserved_handlers;
};

struct manapi::ethreadpool::data_t {
    int m_flags;

    std::vector <std::move_only_function<void()>> m_tasks;

    std::vector <std::coroutine_handle<>> m_handlers;

    std::move_only_function<void()> m_ontask;

    std::shared_ptr<manapi::logger> m_logger;

    std::size_t m_reserved_tasks;

    std::size_t m_reserved_handlers;
};

union threadpool__source {
    ~threadpool__source() {}

    std::move_only_function<void()> cb{};
    std::coroutine_handle<> handle;
};

static void task_doit(std::move_only_function<void()>&task, manapi::logger *logger) MANAPIHTTP_NOEXCEPT {
    try {
        assert((task));
        try {
            task();
        }
        catch (const manapi::exception &e) {
            if (logger) logger->fwarning( "unexpected exception in the task with error code %d: %s", static_cast<int>(e.err_num()), e.what());
        }
        catch (const std::exception &e) {
            if (logger) logger->fwarning( "unexpected exception in the task: %s", e.what());
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "threadpool", e.what());
    }
}

static void task_doit(std::coroutine_handle<> &task, manapi::logger *logger) MANAPIHTTP_NOEXCEPT {
    try {
        assert((task));

        try {
            manapi::async::coro_resume(task);
        }
        catch (const manapi::exception &e) {
            if (logger) logger->warning(std::format("unexpected exception in the task with error code {}: {}", static_cast<int>(e.err_num()), e.what()));
        }
        catch (const std::exception &e) {
            if (logger) logger->warning(std::format("unexpected exception in the task: {}", e.what()));
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "threadpool", e.what());
    }
}


static int mthreadpool_get_task(manapi::mthreadpool::data_t *data, std::size_t index, threadpool__source *source) MANAPIHTTP_NOEXCEPT {
    std::lock_guard<std::mutex> lk (data->m_queue_mutex);

    if (data->m_tasks_by_thread[index].empty()) {
        if (!data->m_tasks.empty()) {
            new (&source->cb) decltype(source->cb) (std::move(data->m_tasks.back()));
            data->m_tasks.pop_back ();
            return 1;
        }
    }
    else {
        new (&source->cb) decltype(source->cb) (std::move(data->m_tasks_by_thread[index].back()));
        data->m_tasks_by_thread[index].pop_back();
        return 1;
    }

    if (!data->m_handlers.empty()) {
        new (&source->handle) decltype(source->handle) (data->m_handlers.back());
        data->m_handlers.pop_back ();
        return 2;
    }

    return 0;
}

static void mthreadpool_run(manapi::mthreadpool::data_t *data, std::size_t index) {
    threadpool__source source;

    while ((data->m_flags & THREADPOOL__FLAG_ACTIVE)) {
        switch (mthreadpool_get_task(data, index, &source)) {
            case 0: {
                std::unique_lock<std::mutex> lk (data->m_m);
                data->m_cv.wait(lk);
                break;
            }
            case 1:
                manapi::async::internal::current_stack_cnt_set(0);
                task_doit(source.cb, nullptr);
                source.cb.~move_only_function();
            break;
            case 2:
                manapi::async::internal::current_stack_cnt_set(0);
                task_doit(source.handle, nullptr);
                source.handle.~coroutine_handle();
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
        this->m_data->m_handlers.reserve(512);
        this->m_data->m_tasks.reserve(512);
    }

    
    mthreadpool::~mthreadpool() = default;

    
    void mthreadpool::resize(std::size_t thread_num) {
        if (!(this->m_data->m_flags & THREADPOOL__FLAG_ACTIVE)) {
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
        if (!(this->m_data->m_flags & THREADPOOL__FLAG_ACTIVE)) {
            return;
        }
        this->m_data->m_flags.fetch_xor(THREADPOOL__FLAG_ACTIVE);
        this->m_data->m_cv.notify_all();
    }

    
    std::size_t mthreadpool::size() const MANAPIHTTP_NOEXCEPT {
        return this->m_data->m_threadnum;
    }

    std::size_t mthreadpool::tasks_size() const MANAPIHTTP_NOEXCEPT {
        std::lock_guard<std::mutex> lk (this->m_data->m_queue_mutex);
        return this->m_data->m_tasks.size() + this->m_data->m_handlers.size();
    }

    void mthreadpool::clear() {
        if (!(this->m_data->m_flags & THREADPOOL__FLAG_ACTIVE)) {
            this->m_data->m_tasks.clear();
            this->m_data->m_handlers.clear();
        }
    }

    
    void mthreadpool::join() MANAPIHTTP_NOEXCEPT {
        for (auto &thread: this->m_data->m_threads) {
            thread.join();
        }

        this->m_data->m_threads.clear();
    }

    
    void mthreadpool::for_all_threads(std::move_only_function<void(tasks_by_thread_t *)> &&cb) {
        {
            std::lock_guard<std::mutex> lk (this->m_data->m_queue_mutex);
            cb (&this->m_data->m_tasks_by_thread);
        }

        cb = nullptr;
        this->m_data->m_cv.notify_all();
    }

    
    void mthreadpool::start() {
        if (this->m_data->m_flags & THREADPOOL__FLAG_ACTIVE) {
            return;
        }

        this->m_data->m_flags.fetch_or(THREADPOOL__FLAG_ACTIVE);

        for (std::size_t i = 0; i < this->m_data->m_threadnum; ++i) {
            this->m_data->m_threads.emplace_back(mthreadpool_worker, this->m_data.get(), i);
        }
    }

    
    void mthreadpool::append_task(std::move_only_function<void()> &&cb) {

        {
            // obtain a mutex
            std::lock_guard<std::mutex> lk (this->m_data->m_queue_mutex);

            assert(!!cb);
            assert(this->m_data->m_tasks.capacity() >= this->m_data->m_reserved_tasks);
            if (this->m_data->m_tasks.capacity() - this->m_data->m_reserved_tasks == 0) {
                this->m_data->m_tasks.resize(this->m_data->m_tasks.size() + THREADPOOL__PREALLOC_ALIGN);
            }

            this->m_data->m_tasks.push_back(std::forward<decltype(cb)>(cb));
        }

        this->m_data->m_cv.notify_one();
    }

    void mthreadpool::append_task(const std::coroutine_handle<> &handle) {
        {
            // obtain a mutex
            std::lock_guard<std::mutex> lk (this->m_data->m_queue_mutex);

            assert(this->m_data->m_handlers.capacity() >= this->m_data->m_reserved_handlers);
            if (this->m_data->m_handlers.capacity() - this->m_data->m_reserved_handlers == 0) {
                this->m_data->m_handlers.resize(this->m_data->m_handlers.size() + THREADPOOL__PREALLOC_ALIGN);
            }

            this->m_data->m_handlers.push_back(handle);
        }

        this->m_data->m_cv.notify_one();
    }

    void mthreadpool::reserve_tasks(manapi::task_types type, std::size_t sz) {
        std::lock_guard<std::mutex> lk (this->m_data->m_queue_mutex);
        std::size_t rez;
        switch (type) {
            case TASK_TYPE_FUNC:
                rez = this->m_data->m_tasks.size() + sz;
                this->m_data->m_tasks.reserve((rez + (THREADPOOL__PREALLOC_ALIGN - 1)) & ~(THREADPOOL__PREALLOC_ALIGN - 1));
                this->m_data->m_reserved_tasks += sz;
                break;
            case TASK_TYPE_HANDLE:
                rez = this->m_data->m_handlers.size() + sz;
                this->m_data->m_handlers.reserve((rez + (THREADPOOL__PREALLOC_ALIGN - 1)) & ~(THREADPOOL__PREALLOC_ALIGN - 1));
                this->m_data->m_reserved_handlers += sz;
                break;
        }
    }

    void mthreadpool::release_tasks(manapi::task_types type, std::size_t sz) MANAPIHTTP_NOEXCEPT {
        std::lock_guard<std::mutex> lk (this->m_data->m_queue_mutex);
        switch (type) {
            case TASK_TYPE_FUNC:
                assert(this->m_data->m_reserved_tasks >= sz);
                this->m_data->m_reserved_tasks -= sz;
                break;
            case TASK_TYPE_HANDLE:
                assert(this->m_data->m_reserved_handlers >= sz);
                this->m_data->m_reserved_handlers -= sz;
                break;
        }
    }

    ethreadpool::ethreadpool(std::shared_ptr<manapi::logger> logger, std::move_only_function<void()> ontask) {
        this->m_data = std::make_unique<data_t>();
        this->m_data->m_logger = std::move(logger);
        this->m_data->m_flags = 0;
        this->m_data->m_ontask = std::move(ontask);
        this->m_data->m_handlers.reserve(4096);
        this->m_data->m_tasks.reserve(1024);
    }

    
    ethreadpool::~ethreadpool() = default;

    std::size_t ethreadpool::tasks_size() const MANAPIHTTP_NOEXCEPT {
        return this->m_data->m_tasks.size() + this->m_data->m_handlers.size();
    }


    bool ethreadpool::try_task() {
        if (!this->m_data->m_tasks.empty()) {
            auto task = std::move(this->m_data->m_tasks.back());
            this->m_data->m_tasks.pop_back();

            manapi::async::internal::current_stack_cnt_set(0);
            task_doit(task, this->m_data->m_logger.get());

            return true;
        }

        if(!this->m_data->m_handlers.empty()) {
            auto task = this->m_data->m_handlers.back();
            this->m_data->m_handlers.pop_back();

            manapi::async::internal::current_stack_cnt_set(0);
            task_doit(task, this->m_data->m_logger.get());

            return true;
        }

        return false;
    }

    
    void ethreadpool::set_notify() MANAPIHTTP_NOEXCEPT {
        this->m_data->m_flags |= THREADPOOL__FLAG_NOTIFY;
    }

    
    void ethreadpool::set_notify_cb(std::move_only_function<void()> ontask) MANAPIHTTP_NOEXCEPT {
        this->m_data->m_ontask = std::move(ontask);
    }

    
    void ethreadpool::stop() MANAPIHTTP_NOEXCEPT {
        if (!(this->m_data->m_flags & THREADPOOL__FLAG_ACTIVE)) {
            return;
        }

        this->m_data->m_flags ^= THREADPOOL__FLAG_ACTIVE;
    }

    
    void ethreadpool::start() {
        if (this->m_data->m_flags & THREADPOOL__FLAG_ACTIVE) {
            return;
        }

        this->m_data->m_flags |= THREADPOOL__FLAG_ACTIVE;
    }


    void ethreadpool::append_task(std::move_only_function<void()> &&cb) {

        assert(!!cb);
        assert(this->m_data->m_tasks.capacity() >= this->m_data->m_reserved_tasks);
        if (this->m_data->m_tasks.capacity() - this->m_data->m_reserved_tasks == 0) {
            this->m_data->m_tasks.resize(this->m_data->m_tasks.size() + (THREADPOOL__PREALLOC_ALIGN - 1));
        }
        
        this->m_data->m_tasks.push_back(std::forward<decltype(cb)>(cb));

        if ((this->m_data->m_flags & THREADPOOL__FLAG_NOTIFY) && this->m_data->m_ontask) {
            try {
                this->m_data->m_flags ^= THREADPOOL__FLAG_NOTIFY;
                this->m_data->m_ontask();
            }
            catch (std::exception const &e) {
                manapi_log_error("%s failed due to %s", "event thread pool: ontask()", e.what());
            }
        }
    }

    void ethreadpool::append_task(const std::coroutine_handle<> &handle) {

        assert(this->m_data->m_handlers.capacity() >= this->m_data->m_reserved_handlers);
        if (this->m_data->m_handlers.capacity() - this->m_data->m_reserved_handlers == 0) {
            this->m_data->m_handlers.resize(this->m_data->m_handlers.size() + THREADPOOL__PREALLOC_ALIGN);
        }

        this->m_data->m_handlers.push_back(handle);

        if ((this->m_data->m_flags & THREADPOOL__FLAG_NOTIFY) && this->m_data->m_ontask) {
            try {
                this->m_data->m_flags ^= THREADPOOL__FLAG_NOTIFY;
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

    void ethreadpool::reserve_tasks(manapi::task_types type, std::size_t sz) {
        std::size_t rez;
        switch (type) {
            case TASK_TYPE_FUNC:
                rez = this->m_data->m_tasks.size() + sz;
                this->m_data->m_tasks.reserve((rez + (THREADPOOL__PREALLOC_ALIGN - 1)) & ~(THREADPOOL__PREALLOC_ALIGN - 1));
                this->m_data->m_reserved_tasks += sz;
                break;
            case TASK_TYPE_HANDLE:
                rez = this->m_data->m_handlers.size() + sz;
                this->m_data->m_handlers.reserve((rez + (THREADPOOL__PREALLOC_ALIGN - 1)) & ~(THREADPOOL__PREALLOC_ALIGN - 1));
                this->m_data->m_reserved_handlers += sz;
                break;
        }
    }

    void ethreadpool::release_tasks(manapi::task_types type, std::size_t sz) MANAPIHTTP_NOEXCEPT {
        switch (type) {
            case TASK_TYPE_FUNC:
                assert(this->m_data->m_reserved_tasks >= sz);
                this->m_data->m_reserved_tasks -= sz;
                break;
            case TASK_TYPE_HANDLE:
                assert(this->m_data->m_reserved_handlers >= sz);
                this->m_data->m_reserved_handlers -= sz;
                break;
        }
    }
}
