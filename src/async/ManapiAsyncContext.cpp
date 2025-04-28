#include "async/ManapiAsyncContext.hpp"
#include <stacktrace>

#include "services/ManapiEventLoop.hpp"
#include "services/ManapiTimerPool.hpp"

#include "ManapiInitTools.hpp"

async::shared_ctx manapi::async::context::gctx = nullptr;

manapi::async::context::context(std::shared_ptr<event_loop> watcher, std::shared_ptr<threadpool<task>> taskpool, std::shared_ptr<manapi::timerpool> timerpool, std::shared_ptr<manapi::logger> logger)  {
    this->watcher_ = std::move(watcher);
    this->taskpool_ = std::move(taskpool);
    this->logger_ = std::move(logger);
    this->timerpool_ = std::move(timerpool);
}

async::shared_ctx manapi::async::context::create(const unsigned int &threadnum, const ssize_t &timer_delay) {
    manapi::init_tools::ssl_library_init();
    manapi::init_tools::ev_library_init();
    manapi::init_tools::curl_library_init();

    auto logger_ = std::make_shared<manapi::logger>();
    auto taskpool_ = std::make_shared<manapi::threadpool<task>>(logger_, threadnum, 1);
    auto watcher_ = std::make_shared<manapi::event_loop>(taskpool_, logger_);
    auto timerpool_ = std::make_shared<manapi::timerpool>(watcher_, timer_delay);

    auto ctx = std::make_shared<context>(std::move(watcher_), std::move(taskpool_), std::move(timerpool_), std::move(logger_));
    ctx->weak = ctx;

    return std::move(ctx);
}

manapi::future<void> manapi::async::context::start() {
    this->taskpool_->start();
    co_await this->timerpool_->start(this->timerpool_);
    co_await this->watcher_->start(this->watcher_);
}

void manapi::async::context::sync_start() {
    this->taskpool()->start();
    async::run(std::shared_ptr<context>(this->weak), this->timerpool_->start(this->timerpool_));
    this->watcher_->sync_start(this->watcher_);
}

manapi::future<void> manapi::async::context::stop() {
    co_await this->timerpool_->stop();
    co_await this->watcher_->stop();

    this->taskpool_->stop();
}

void manapi::async::context::join() {
    this->taskpool_->join();
}

const std::shared_ptr<manapi::event_loop> & manapi::async::context::eventloop() {
    return this->watcher_;
}

const std::shared_ptr<manapi::threadpool<manapi::task>> & manapi::async::context::taskpool() {
    return this->taskpool_;
}

const std::shared_ptr<manapi::timerpool> & manapi::async::context::timerpool() {
    return this->timerpool_;
}

const std::shared_ptr<manapi::logger> & manapi::async::context::logger() {
    return this->logger_;
}

manapi::async::context::~context() = default;

void manapi::async::run_prepare_(const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<> &task, std::unique_ptr<async_task_t> task_data, std::move_only_function<void()> onfinish)  {
#if defined(MANAPIHTTP_ASYNC_DEBUG)
    auto index = reinterpret_cast <size_t> (task.get_handle().address());
    task.on_finish([index, taskpool=taskpool, onfinish = std::move(onfinish)] () mutable -> void {
        auto index_ = index;
#else
    task.on_finish([task_data = std::move(task_data), onfinish = std::move(onfinish)] () mutable -> void {
#endif
        if (onfinish) {
            try { onfinish(); }
            catch (std::exception const &e) {
                std::cerr << e.what() << "\n";
            }
        }
#if defined(MANAPIHTTP_ASYNC_DEBUG)
        decltype(async_tasks)::node_type data;
        {
            std::lock_guard<std::mutex> lk (async_tasks_mx);
            auto it = async_tasks.find(index_);
            if (it != async_tasks.end()) {
                data = std::move(async_tasks.extract(it));
            }
        }
#else

#endif

    }, taskpool);

    task();
}

manapi::future<> manapi::async::blank_future () {
    co_return;
}

void manapi::async::run(const std::shared_ptr<context> &ctx, manapi::future<> task,std::move_only_function<void()> onfinish) {
    async::run (ctx->taskpool(), std::move(task), std::move(onfinish));
}

void manapi::async::run(const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<> task, std::move_only_function<void()> onfinish) {
    auto handle = task.release();

    auto task_data = std::make_unique<async_task_t>(manapi::future<>{handle});

#if defined(MANAPIHTTP_ASYNC_DEBUG)
    {
        auto index = reinterpret_cast <size_t> (handle.address());
        std::lock_guard<std::mutex> lk (async_tasks_mx);
        auto status = async_tasks.insert({index, task_data.get()});
        assert(status.second);
    }
#endif

    task = manapi::future<>{handle};
    async::run_prepare_(taskpool, task, std::move(task_data), std::move(onfinish));

    task.release();
}

const std::shared_ptr<manapi::threadpool<manapi::task>> & manapi::async::as_threadpool(const std::shared_ptr<context> &ctx) {
    return ctx->taskpool();
}
