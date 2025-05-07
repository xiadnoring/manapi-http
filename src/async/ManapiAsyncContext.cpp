#include "async/ManapiAsyncContext.hpp"

#include <stacktrace>
#include <uv.h>

#include "services/ManapiEventLoop.hpp"
#include "services/ManapiTimerPool.hpp"

#include "ManapiInitTools.hpp"

#include "../include/ManapiDefaultErrors.hpp"

manapi::async::shared_ctx manapi::async::context::gctx = nullptr;
std::unique_ptr<manapi::sigset_t> manapi::async::context::gbs = nullptr;

manapi::async::context::context(shared_async_thread main, std::shared_ptr<mthreadpool<task>> taskpool, shared_logger logger)  {
    this->main_ = std::move(main);
    this->taskpool_ = std::move(taskpool);
    this->logger_ = std::move(logger);
}

manapi::async::shared_ctx manapi::async::context::create(unsigned int threadnum, ssize_t timer_delay) {
    manapi::init_tools::ssl_library_init();
    manapi::init_tools::ev_library_init();
    manapi::init_tools::curl_library_init();

    auto logger_ = std::make_shared<manapi::logger>();
    auto taskpool_ = std::make_shared<manapi::mthreadpool<task>>(logger_, threadnum);

    /* Main Event Loop */
    auto watcher_ = std::make_shared<manapi::event_loop>(taskpool_, logger_);
    auto timerpool_ = std::make_shared<manapi::timerpool>(watcher_, timer_delay);

    auto main_ = std::make_shared<async_thread_t>(std::move(watcher_), std::move(timerpool_));

    auto ctx = std::make_shared<context>(std::move(main_), std::move(taskpool_), std::move(logger_));
    ctx->weak = ctx;

    return std::move(ctx);
}

void manapi::async::context::threadpoolfs(std::size_t cnt) {
    auto s = std::to_string(cnt);
    assert(!uv_os_setenv("UV_THREADPOOL_SIZE", s.data()));
}

std::unique_ptr<manapi::sigset_t> manapi::async::context::blockedsignals() {
    auto blocked_signals = std::make_unique<manapi::sigset_t>();
#if defined (__unix__) || defined(__APPLE__)
    sigemptyset(blocked_signals.get());
    sigaddset(blocked_signals.get(), SIGPIPE);
    pthread_sigmask(SIG_BLOCK, blocked_signals.get(), nullptr);
#endif
    return std::move(blocked_signals);
}

manapi::future<void> manapi::async::context::start() {
    this->taskpool_->start();
    co_await this->main_->timerpool->start(this->main_->timerpool);
    co_await this->main_->eventloop->start(this->main_->eventloop);
}

void manapi::async::context::sync_start() {
    this->taskpool_->start();
    async::run(this->taskpool(),
        this->main_->timerpool->start(this->main_->timerpool));
    this->main_->eventloop->sync_start(this->main_->eventloop);
}

manapi::future<void> manapi::async::context::stop() {
    co_await this->main_->eventloop->stop();
    co_await this->main_->timerpool->stop();

    this->taskpool_->stop();
}

void manapi::async::context::join() {
    this->taskpool_->join();
}

const std::shared_ptr<manapi::event_loop> & manapi::async::context::eventloop() {
    return this->main_->eventloop;
}

const std::shared_ptr<manapi::threadpool<manapi::task>> & manapi::async::context::taskpool() {
    return this->main_->eventloop->taskpool();
}

const std::shared_ptr<manapi::timerpool> & manapi::async::context::timerpool() {
    return this->main_->timerpool;
}

const std::shared_ptr<manapi::logger> & manapi::async::context::logger() {
    return this->logger_;
}

manapi::async::context::~context() = default;

void manapi::async::internal::run_prepare_error_(const manapi::async::shared_taskpool &taskpool, std::exception_ptr err) {
    int errnum = manapi::ERR_OK;
    std::string errmsg;
    manapi::json data;

    manapi::rethrow_exception_ptr(std::move(err), &errnum, &errmsg, &data);

    taskpool->logger()->error(manapi::logger::default_service, manapi::ERR_UNHANDLED_EXCEPTION,
        manapi::error::default_msgs[manapi::error::ERRMSG_UNHANDLED_EXCEPTION], errnum, std::move(errmsg), data.dump());
}


void manapi::async::internal::run_prepare_std_exception_(const manapi::async::shared_taskpool &taskpool, std::exception const &e) {
    taskpool->logger()->error(manapi::logger::default_service, manapi::ERR_UNHANDLED_EXCEPTION,
                    manapi::error::default_msgs[manapi::error::ERRMSG_UNHANDLED_EXCEPTION], manapi::ERR_UNHANDLED_EXCEPTION, e.what(), "");
}


void manapi::async::internal::run_prepare_manapi_exception_(const manapi::async::shared_taskpool &taskpool, manapi::exception &e) {
    taskpool->logger()->error(manapi::logger::default_service, manapi::ERR_UNHANDLED_EXCEPTION,
                    manapi::error::default_msgs[manapi::error::ERRMSG_UNHANDLED_EXCEPTION], e.err_num(), e.what(), e.data()->dump());
}

const std::shared_ptr<manapi::threadpool<manapi::task>> & manapi::async::internal::as_threadpool(const std::shared_ptr<manapi::async::context> &ctx) {
    return ctx->taskpool();
}
