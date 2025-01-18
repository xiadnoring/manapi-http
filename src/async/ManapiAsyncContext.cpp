#include "async/ManapiAsyncContext.hpp"

#include "services/ManapiEventLoop.hpp"
#include "services/ManapiTimerPool.hpp"

manapi::async::context::context(std::shared_ptr<event_loop> watcher, std::shared_ptr<threadpool<task>> taskpool, std::shared_ptr<manapi::timerpool> timerpool)  {
    this->watcher_ = std::move(watcher);
    this->taskpool_ = std::move(taskpool);
    this->timerpool_ = std::move(timerpool);
}

std::shared_ptr<manapi::async::context> manapi::async::context::create(const unsigned int &threadnum, const double &timer_delay) {
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
