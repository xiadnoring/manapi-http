#include "async/ManapiAsyncContext.hpp"

#include <stacktrace>
#include <uv.h>

#include "services/ManapiEventLoop.hpp"
#include "services/ManapiTimerPool.hpp"

#include "ManapiInitTools.hpp"

#include "../include/ManapiDefaultErrors.hpp"
#include "services/ManapiTaskFunction.hpp"

manapi::async::shared_ctx manapi::async::context::gctx = nullptr;
std::unique_ptr<manapi::sigset_t> manapi::async::context::gbs = nullptr;

manapi::async::cthread::cthread(shared_eventloop eventloop, shared_taskpool taskpool, shared_timerpool timerpool, shared_logger logger) {
    this->eventloop_ = std::move(eventloop);
    this->taskpool_ = std::move(taskpool);
    this->timerpool_ = std::move(timerpool);
    this->logger_ = std::move(logger);
    this->flags = 0;
}

void manapi::async::cthread::current(std::shared_ptr<cthread> thr) {
    async::internal::current_cthread_ = std::move(thr);
}

manapi::async::cthread::~cthread() = default;

// manapi::future<void> manapi::async::cthread::start() {
//     this->taskpool_->start();
//     this->timerpool_->start();
//     co_await this->eventloop_->start(this->eventloop_);
// }

void manapi::async::cthread::sync_start() {
    this->taskpool_->start();
    timerpool()->start();
    this->eventloop_->sync_start(this->eventloop_);
}

manapi::future<void> manapi::async::cthread::stop() {
    co_await this->eventloop_->stop();
    this->timerpool_->stop();
    this->taskpool_->stop();
}

void manapi::async::cthread::join() {
    this->taskpool_->join();
}

const std::shared_ptr<manapi::event_loop> & manapi::async::cthread::eventloop() {
    return this->eventloop_;
}

// const std::shared_ptr<manapi::threadpool<manapi::task>> & manapi::async::cthread::taskpool() {
//     return this->taskpool_;
// }

const std::shared_ptr<manapi::timerpool> & manapi::async::cthread::timerpool() {
    return this->timerpool_;
}

const manapi::async::shared_taskpool & manapi::async::cthread::etaskpool() {
    return this->eventloop_->taskpool();
}

const std::shared_ptr<manapi::logger> & manapi::async::cthread::logger() {
    return this->logger_;
}

manapi::async::context::context(shared_eventloop eventloop, std::shared_ptr<mthreadpool<task>> taskpool, shared_timerpool timerpool, shared_logger logger)
    : cthread(std::move(eventloop), std::move(taskpool), std::move(timerpool), std::move(logger))  {

}

// void manapi::async::context::inloops(shared_ctx thr, std::function<void()> callback) {
//     auto loops = thr->loops_;
//     loops.push_back(std::move(thr));
//
//     for (auto const &loop : loops) {
//         /* run the callback in other event loop */
//         manapi::async::run(loop->eventloop()->custom_callback(
//             [this, callback] (event_loop *ev) mutable
//             -> void {
//             callback();
//         }));
//     }
// }

manapi::async::shared_ctx manapi::async::context::create(unsigned int threadnum) {
    manapi::init_tools::ssl_library_init();
    manapi::init_tools::ev_library_init();
    manapi::init_tools::curl_library_init();

    auto logger_ = std::make_shared<manapi::logger>();
    auto taskpool_ = std::make_shared<manapi::mthreadpool<task>>(logger_, threadnum);

    /* Main Event Loop */
    auto watcher_ = std::make_shared<manapi::event_loop>(taskpool_, logger_);
    auto timerpool_ = std::make_shared<manapi::timerpool>(watcher_);

    auto mainctx = std::make_shared<context>(std::move(watcher_), taskpool_, std::move(timerpool_), logger_);

    manapi::async::context::current(mainctx);

    return std::move(mainctx);
}

void manapi::async::context::run(shared_ctx ctx, int loops, std::function<void(std::function<void()> bind)> callback) {
    auto const mtaskpool = dynamic_cast<mthreadpool<task> *> (ctx->taskpool_.get());

    assert((loops <= mtaskpool->size() && "not enough threads for event loops"));

    ctx->loops_.resize(loops);

    for (int i = 0; i < loops; ++i) {
        auto watcher_ = std::make_shared<manapi::event_loop>(ctx->taskpool_, ctx->logger_);
        auto timerpool_ = std::make_shared<manapi::timerpool>(watcher_);

        ctx->loops_[i] = std::make_shared<async::cthread> (std::move(watcher_), ctx->taskpool_, std::move(timerpool_), ctx->logger_);
    }

    if (!manapi::async::current()) {
        manapi::async::context::current(ctx);
    }

    for (int i = 0; i < loops; ++i) {
        mtaskpool->for_all_threads([&] (mthreadpool<task>::tasks_by_thread_t *v)
            -> void {
            (*v)[i].push_back(std::make_unique<manapi::function_task> ([callback, thr = ctx->loops_[i]] ()
                -> void {
                async::cthread::current(thr);
                thr->timerpool()->start();

                callback([thr] () -> void {
                    thr->sync_start();

                    thr->timerpool()->stop();

                    thr->eventloop()->wait();

                    manapi::async::internal::current_cthread_ = nullptr;
                });


            }));
        });
    }


    callback([ctx = std::move(ctx)] () -> void {
        ctx->sync_start();

        ctx->timerpool_->stop();

        ctx->eventloop_->wait();

        ctx->taskpool_->stop();
        ctx->taskpool_->join();

        manapi::async::internal::current_cthread_ = nullptr;
    });
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

const std::vector<manapi::async::shared_cthread> & manapi::async::context::loops() {
    return this->loops_;
}

const manapi::async::shared_cthread &manapi::async::current() {
    return async::internal::current_cthread_;
}


manapi::async::context::~context() = default;

void manapi::async::internal::run_prepare_error_(std::exception_ptr err) {
    int errnum = manapi::ERR_OK;
    std::string errmsg;
    manapi::json data;

    manapi::rethrow_exception_ptr(std::move(err), &errnum, &errmsg, &data);

    manapi::async::current()->logger()->error(manapi::logger::default_service, manapi::ERR_UNHANDLED_EXCEPTION,
        manapi::error::default_msgs[manapi::error::ERRMSG_UNHANDLED_EXCEPTION], errnum, std::move(errmsg), data.dump());
}


void manapi::async::internal::run_prepare_std_exception_(std::exception const &e) {
    manapi::async::current()->logger()->error(manapi::logger::default_service, manapi::ERR_UNHANDLED_EXCEPTION,
                    manapi::error::default_msgs[manapi::error::ERRMSG_UNHANDLED_EXCEPTION], manapi::ERR_UNHANDLED_EXCEPTION, e.what(), "");
}


void manapi::async::internal::run_prepare_manapi_exception_(manapi::exception &e) {
    manapi::async::current()->logger()->error(manapi::logger::default_service, manapi::ERR_UNHANDLED_EXCEPTION,
                    manapi::error::default_msgs[manapi::error::ERRMSG_UNHANDLED_EXCEPTION], e.err_num(), e.what(), e.data()->dump());
}

const std::shared_ptr<manapi::threadpool<manapi::task>> & manapi::async::internal::ethreadpool_(const shared_cthread &ctx) {
    return ctx->eventloop()->taskpool();
}
