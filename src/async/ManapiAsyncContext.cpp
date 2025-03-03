#include "async/ManapiAsyncContext.hpp"
#include <stacktrace>

#include "services/ManapiEventLoop.hpp"
#include "services/ManapiTimerPool.hpp"

#include "ManapiInitTools.hpp"

manapi::async::context::context(std::shared_ptr<event_loop> watcher, std::shared_ptr<threadpool<task>> taskpool, std::shared_ptr<manapi::timerpool> timerpool)  {
    this->watcher_ = std::move(watcher);
    this->taskpool_ = std::move(taskpool);
    this->timerpool_ = std::move(timerpool);
}

std::shared_ptr<manapi::async::context> manapi::async::context::create(const unsigned int &threadnum, const double &timer_delay) {
    manapi::init_tools::ssl_library_init();
    manapi::init_tools::ev_library_init();
    manapi::init_tools::curl_library_init();

    auto taskpool_ = std::make_shared<manapi::threadpool<task>>(threadnum, 1);
    auto watcher_ = std::make_shared<manapi::event_loop>(taskpool_);
    auto timerpool_ = std::make_shared<manapi::timerpool>(watcher_, timer_delay);

    auto ctx = std::make_shared<context>(std::move(watcher_), std::move(taskpool_), std::move(timerpool_));
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

manapi::async::context::~context() = default;

size_t manapi::async::_run_prepare(const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<> &task, std::move_only_function<void()> onfinish)  {
    auto index = reinterpret_cast <size_t> (task.get_handle().address());

    task.on_finish([index, taskpool=taskpool, onfinish = std::move(onfinish)] () mutable -> void {
        auto index_ = index;
        auto taskpool_ = std::move(taskpool);

        if (onfinish) {
            taskpool_->append_task([finish = std::move(onfinish)] () mutable
                -> void { finish(); });
        }

        decltype(async_tasks)::node_type data;
        {
            std::lock_guard<std::mutex> lk (async_tasks_mx);
            auto it = async_tasks.find(index_);
            if (it != async_tasks.end()) {
                data = std::move(async_tasks.extract(it));
            }
        }
    }, taskpool);

    task();

    return index;
}

void manapi::async::run(const std::shared_ptr<context> &ctx, manapi::future<> task,std::move_only_function<void()> onfinish) {
    async::run (ctx->taskpool(), std::move(task), std::move(onfinish));
}

void manapi::async::run(const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<> task, std::move_only_function<void()> onfinish) {
    auto handle = task.release();
    {
        auto index = reinterpret_cast <size_t> (handle.address());
        std::lock_guard<std::mutex> lk (async_tasks_mx);
        auto status = async_tasks.insert({index, std::make_shared<async_task_t>(std::move(manapi::future<>{handle}), "")});
        assert(status.second);
    }

    task = manapi::future<>{handle};
    async::_run_prepare(taskpool, task, std::move(onfinish));

    task.release();
}

const std::shared_ptr<manapi::threadpool<manapi::task>> & manapi::async::as_threadpool(const std::shared_ptr<context> &ctx) {
    return ctx->taskpool();
}
