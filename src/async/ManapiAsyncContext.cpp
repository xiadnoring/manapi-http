#include "async/ManapiAsyncContext.hpp"

#include <uv.h>

#include "services/ManapiEventLoop.hpp"
#include "services/ManapiTimerPool.hpp"

#include "ManapiInitTools.hpp"

#include "../include/ManapiUtils.hpp"
#include "../include/ManapiDefaultErrors.hpp"

manapi::async::shared_ctx manapi::async::context::gctx = nullptr;
std::unique_ptr<manapi::sigset_t> manapi::async::context::gbs = nullptr;

manapi::async::cthread::cthread(shared_eventloop eventloop, shared_taskpool taskpool, shared_timerpool timerpool, shared_logger logger) {
    this->eventloop_ = std::move(eventloop);
    this->taskpool_ = std::move(taskpool);
    this->timerpool_ = std::move(timerpool);
    this->logger_ = std::move(logger);
    this->flags = 0;
}

void manapi::async::cthread::current(std::shared_ptr<cthread> thr) MANAPIHTTP_NOEXCEPT {
    async::internal::current_cthread_ = std::move(thr);
}

manapi::async::cthread::~cthread() = default;

// manapi::future<void> manapi::async::cthread::start() {
//     this->taskpool_->start();
//     this->timerpool_->start();
//     co_await this->eventloop_->start(this->eventloop_);
// }

manapi::sys_error::status manapi::async::cthread::sync_start() {
    this->taskpool_->start();
    auto res = timerpool()->start();
    if (!res)
        return std::move(res);
    return this->eventloop_->sync_start(this->eventloop_);
}

manapi::future<void> manapi::async::cthread::stop() {
    co_await this->eventloop_->stop();
    this->timerpool_->stop();
    this->taskpool_->stop();
}

void manapi::async::cthread::join() MANAPIHTTP_NOEXCEPT {
    this->taskpool_->join();
}

const std::shared_ptr<manapi::event_loop> & manapi::async::cthread::eventloop() MANAPIHTTP_NOEXCEPT {
    return this->eventloop_;
}

// const std::shared_ptr<manapi::threadpool<manapi::task>> & manapi::async::cthread::taskpool() {
//     return this->taskpool_;
// }

const std::shared_ptr<manapi::timerpool> & manapi::async::cthread::timerpool() MANAPIHTTP_NOEXCEPT {
    return this->timerpool_;
}

const manapi::async::shared_taskpool & manapi::async::cthread::etaskpool() MANAPIHTTP_NOEXCEPT {
    return this->eventloop_->taskpool();
}

const std::shared_ptr<manapi::logger> & manapi::async::cthread::logger() MANAPIHTTP_NOEXCEPT {
    return this->logger_;
}

manapi::object_pool & manapi::async::cthread::memory_fabric() MANAPIHTTP_NOEXCEPT {
    return this->memory_fabric_;
}

manapi::async::context::context(shared_eventloop eventloop, std::shared_ptr<mthreadpool> taskpool, shared_timerpool timerpool, shared_logger logger)
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

manapi::error::status_or<manapi::async::shared_ctx> manapi::async::context::create(unsigned int threadnum) MANAPIHTTP_NOEXCEPT {
    try {
        auto logger_ = std::make_shared<manapi::logger>();
        auto taskpool_ = std::make_shared<manapi::mthreadpool>(logger_, threadnum);

        /* Main Event Loop */
        auto watcher_ = manapi::event_loop::create(taskpool_, logger_).unwrap();
        auto timerpool_ = std::make_shared<manapi::timerpool>(manapi::timerpool::create(watcher_).unwrap());

        auto mainctx = std::make_shared<context>(std::move(watcher_), taskpool_, std::move(timerpool_), logger_);

        manapi::async::context::current(mainctx);

        return std::move(mainctx);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "ctx:create failed", e.what());
        return error::status_internal("ctx:create failed");
    }
}

manapi::error::status manapi::async::context::run(shared_ctx ctx, uint32_t loops, std::function<void(std::function<void()> bind)> callback) MANAPIHTTP_NOEXCEPT {
    try {
        auto const mtaskpool = dynamic_cast<mthreadpool *> (ctx->taskpool_.get());

        if (loops > mtaskpool->size()) {
            loops = mtaskpool->size();
            manapi_log_info("not enough threads for additional event loops. available: %zu", mtaskpool->size());
        }

        ctx->loops_.resize(loops);

        try {
            for (int i = 0; i < loops; ++i) {
                auto watcher_ = manapi::event_loop::create(ctx->taskpool_, ctx->logger_).unwrap();
                auto timerpool_ = std::make_shared<manapi::timerpool>(manapi::timerpool::create(watcher_).unwrap());

                ctx->loops_[i] = std::make_shared<async::cthread> (std::move(watcher_), ctx->taskpool_, std::move(timerpool_), ctx->logger_);
            }
        }
        catch (std::exception const &) {
            ctx->loops_.clear();

            std::rethrow_exception(std::current_exception());
        }

        //assert(!manapi::async::internal::current_() && "Async ctx already exists");

        if (!manapi::async::internal::current_())
            manapi::async::context::current(ctx);


        manapi::init_tools::ssl_library_init();
        manapi::init_tools::ev_library_init();
        manapi::init_tools::curl_library_init();

        for (int i = 0; i < loops; ++i) {
            mtaskpool->for_all_threads([&] (mthreadpool::tasks_by_thread_t *v)
                -> void {
                manapi::init_tools::ssl_library_init();
                manapi::init_tools::ev_library_init();
                manapi::init_tools::curl_library_init();

                (*v)[i].push_back([callback, thr = ctx->loops_[i]] ()
                    -> void {
                    async::cthread::current(thr);

                    callback([thr] () -> void {
                        thr->sync_start().unwrap();

                        thr->timerpool()->stop();

                        thr->eventloop()->wait();

                        manapi::async::context::current(nullptr);

                        manapi::clear_tools::ssl_library_thread_clear();
                    });
                });
            });
        }


        auto tres= manapi::async::current()->timerpool()->append_interval_sync(60 * 1000,
            [] (const manapi::timer &t) -> void {
            manapi::async::current()->memory_fabric().clear();
        });

        if (!tres)
            tres.err().log();

        callback([ctx] () -> void {
            ctx->sync_start().unwrap();

            ctx->timerpool_->stop();

            ctx->eventloop_->wait();

            ctx->taskpool_->stop();
            ctx->taskpool_->join();

            manapi::async::context::current(nullptr);
        });

        return error::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "ctx:run failed", e.what());
    }

    manapi::async::context::current(nullptr);
    return error::status_internal("ctx:run failed");
}

void manapi::async::context::run(shared_ctx ctx, std::function<void(std::function<void()> bind)> callback) {
    manapi::async::context::run(std::move(ctx), 0, std::move(callback));
}

void manapi::async::context::threadpoolfs(std::size_t cnt) MANAPIHTTP_NOEXCEPT {
    auto s = std::to_string(cnt);
    if (auto rhs = uv_os_setenv("UV_THREADPOOL_SIZE", s.data())) {
        manapi_log_trace("%s failed due to %s", "ctx:set UV_THREADPOOL_SIZE", ev::strerror(rhs));
    }
}

std::unique_ptr<manapi::sigset_t> manapi::async::context::blockedsignals() MANAPIHTTP_NOEXCEPT {
    auto blocked_signals = std::make_unique<manapi::sigset_t>();
#if defined (__unix__) || defined(__APPLE__)
    sigemptyset(blocked_signals.get());
    sigaddset(blocked_signals.get(), SIGPIPE);
    pthread_sigmask(SIG_BLOCK, blocked_signals.get(), nullptr);
#endif
    return std::move(blocked_signals);
}

const std::vector<manapi::async::shared_cthread> & manapi::async::context::loops() MANAPIHTTP_NOEXCEPT {
    return this->loops_;
}

const manapi::async::shared_cthread &manapi::async::current() MANAPIHTTP_NOEXCEPT {
   assert(async::internal::current_cthread_ && "async ctx doesn't exists in that thread");
    return async::internal::current_cthread_;
}


const std::shared_ptr<manapi::async::cthread> & manapi::async::internal::current_() MANAPIHTTP_NOEXCEPT {
    return async::internal::current_cthread_;
}

manapi::async::context::~context() = default;

void manapi::async::internal::run_prepare_error_(std::exception_ptr err) MANAPIHTTP_NOEXCEPT {
    try {
        int errnum = manapi::ERR_OK;
        std::string errmsg;

        manapi::extract_exception_ptr(std::move(err), &errnum, &errmsg);

        manapi::async::current()->logger()->error(manapi::logger::default_service, manapi::ERR_UNKNOWN,
            manapi::error::default_msgs[manapi::error::ERRMSG_UNHANDLED_EXCEPTION], errnum, std::move(errmsg));
    }
    catch (std::exception const &ex) {
        manapi_log_error("%s due to %s", "logger failed", ex.what());
    }
}


void manapi::async::internal::run_prepare_std_exception_(std::exception const &e) MANAPIHTTP_NOEXCEPT {
    try {
        manapi::async::current()->logger()->error(manapi::logger::default_service, manapi::ERR_UNKNOWN,
                        manapi::error::default_msgs[manapi::error::ERRMSG_UNHANDLED_EXCEPTION], manapi::ERR_UNKNOWN, e.what(), "");
    }
    catch (std::exception const &ex) {
        manapi_log_error("%s due to %s", "logger failed", ex.what());
    }
}


void manapi::async::internal::run_prepare_manapi_exception_(manapi::exception &e) MANAPIHTTP_NOEXCEPT {
    try {
        manapi::async::current()->logger()->error(manapi::logger::default_service, manapi::ERR_UNKNOWN,
                        manapi::error::default_msgs[manapi::error::ERRMSG_UNHANDLED_EXCEPTION], e.err_num(), e.what());
    }
    catch (std::exception const &ex) {
        manapi_log_error("%s due to %s", "logger failed", ex.what());
    }
}

const std::shared_ptr<manapi::threadpool> & manapi::async::internal::ethreadpool_(const shared_cthread &ctx) MANAPIHTTP_NOEXCEPT {
    return ctx->eventloop()->taskpool();
}
