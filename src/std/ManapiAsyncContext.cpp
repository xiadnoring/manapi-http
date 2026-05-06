#include "ManapiEventLoop.hpp"
#include "ManapiTimerPool.hpp"
#include "ManapiInitTools.hpp"
#include "ManapiEventLoop.hpp"
#include "ManapiTimerPool.hpp"
#include "ManapiThreadPool.hpp"
#include "std/ManapiAsyncLogger.hpp"
#include "std/ManapiAsyncContext.hpp"

#include <thread>

#include "../include/ManapiUtils.hpp"
#include "../include/ManapiDefaultErrors.hpp"

manapi::async::shared_ctx manapi::async::context::gctx_ = nullptr;
std::unique_ptr<manapi::sigset_t> manapi::async::context::gbs_ = nullptr;

manapi::async::cthread::cthread(shared_eventloop eventloop, shared_mthreadpool taskpool, shared_timerpool timerpool, shared_logger logger) {
    this->eventloop_ = std::move(eventloop);
    this->taskpool_ = std::move(taskpool);
    this->timerpool_ = std::move(timerpool);
    this->logger_ = std::move(logger);
    this->flags = 0;
}

void manapi::async::cthread::current(std::shared_ptr<cthread> thr) MANAPIHTTP_NOEXCEPT {
    internal::current_(std::move(thr));
}

manapi::async::cthread::~cthread() = default;

// manapi::future<void> manapi::async::cthread::start() {
//     this->taskpool_->start();
//     this->timerpool_->start();
//     co_await this->eventloop_->start(this->eventloop_);
// }

manapi::ev::status manapi::async::cthread::start() {
    this->taskpool_->start();
    auto res = timerpool()->start();
    if (!res)
        return std::move(res);
    auto sys_res = this->eventloop_->start();
    if (sys_res.code() != ERR_ABORTED) {
        return std::move(sys_res);
    }
    return ev::status_ok();
}

manapi::future<void> manapi::async::cthread::stop() {
    co_await this->eventloop_->stop();
    this->timerpool_->stop();
    this->taskpool_->stop();

    manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "cthread has been stopped");
}

void manapi::async::cthread::join() MANAPIHTTP_NOEXCEPT {
    this->taskpool_->join();
}

const std::shared_ptr<manapi::event_loop> & manapi::async::cthread::eventloop() MANAPIHTTP_NOEXCEPT {
    return this->eventloop_;
}

const manapi::async::shared_mthreadpool& manapi::async::cthread::threadpool() {
    return this->taskpool_;
}

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

manapi::status_or<manapi::async::shared_ctx> manapi::async::context::create(std::size_t threadnum) MANAPIHTTP_NOEXCEPT {
    try {
        auto logger_ = std::make_shared<manapi::logger>();
        auto taskpool_ = std::make_shared<manapi::mthreadpool>(threadnum);

        /* Main Event Loop */
        auto watcher_ = manapi::event_loop::create(taskpool_, logger_).unwrap();
        auto timerpool_ = manapi::timerpool::create(watcher_).unwrap();

        auto mainctx = std::shared_ptr<context> (new context (std::move(watcher_), taskpool_, std::move(timerpool_), logger_));

        manapi::async::context::current(mainctx);

        return std::move(mainctx);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "ctx:create failed", e.what());
        return status_internal("ctx:create failed");
    }
}

manapi::status manapi::async::context::run(std::size_t loops, std::function<void(std::function<void()> bind)> callback) MANAPIHTTP_NOEXCEPT {
    try {
        auto ctx = this->shared_from_this();

        if (!ctx) {
            return manapi::status_not_found("context:not found");
        }

        ctx->eventloop()->setup_handle_interrupt();

        auto const mtaskpool = (ctx->taskpool_.get());

        if (loops > mtaskpool->size()) {
            loops = mtaskpool->size();
            manapi_log_info("not enough threads for the additional event loops. Reduce to %zu", mtaskpool->size());
        }

        ctx->loops_.resize(loops);

        try {
            for (std::size_t i = 0; i < loops; ++i) {
                auto watcher_ = manapi::event_loop::create(ctx->taskpool_, ctx->logger_).unwrap();
                auto timerpool_ = manapi::timerpool::create(watcher_).unwrap();

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

        for (std::size_t i = 0; i < loops; ++i) {
            mtaskpool->for_all_threads([&] (mthreadpool::tasks_by_thread_t *v)
                -> void {
                manapi::init_tools::ssl_library_init();
                manapi::init_tools::ev_library_init();
                manapi::init_tools::curl_library_init();

                (*v)[i].push_back([callback, thr = ctx->loops_[i]] ()
                    -> void {
                    async::cthread::current(thr);

                    callback([thr] () -> void {
                        thr->eventloop()->m_etaskpool->start();
                        thr->start().unwrap();

                        thr->timerpool()->stop();

                        manapi::clear_tools::grpc_clear();

                        thr->eventloop()->wait_all(false);
                    });

                    thr->eventloop()->wait_all(true);

                    thr->eventloop()->m_etaskpool->stop();
                    thr->eventloop()->m_etaskpool->join();

                    manapi::async::context::current(nullptr);

                    manapi::clear_tools::ssl_library_thread_clear();
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
            ctx->eventloop()->m_etaskpool->start();
            ctx->start().unwrap();

            ctx->timerpool_->stop();

            manapi::clear_tools::grpc_clear();

            ctx->eventloop_->wait_all(false);

        });

        ctx->eventloop_->wait_all(true);
        ctx->eventloop()->m_etaskpool->stop();
        ctx->eventloop()->m_etaskpool->join();

        ctx->taskpool_->stop();
        ctx->taskpool_->join();

        manapi::async::context::current(nullptr);

        return status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "ctx:run failed", e.what());
    }

    manapi::async::context::current(nullptr);
    return status_internal("ctx:run failed");
}

void manapi::async::context::run(std::function<void(std::function<void()> bind)> callback) {
    this->run(0, std::move(callback));
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

manapi::sigset_t *manapi::async::context::gbs () MANAPIHTTP_NOEXCEPT {
    return manapi::async::context::gbs_.get();
}

void manapi::async::context::gbs(std::unique_ptr<manapi::sigset_t> n) MANAPIHTTP_NOEXCEPT {
    manapi::async::context::gbs_ = std::move(n);
}

std::shared_ptr<manapi::async::context> manapi::async::context::gctx () MANAPIHTTP_NOEXCEPT {
    return manapi::async::context::gctx_;
}

void manapi::async::context::gctx (std::shared_ptr<async::context> n) MANAPIHTTP_NOEXCEPT {
    manapi::async::context::gctx_ = std::move(n);
}

const std::vector<manapi::async::shared_cthread> & manapi::async::context::loops() MANAPIHTTP_NOEXCEPT {
    return this->loops_;
}

const manapi::async::shared_cthread &manapi::async::current() MANAPIHTTP_NOEXCEPT {
    assert(manapi::async::internal::current_() && "async ctx doesn't exist in that thread");
    return manapi::async::internal::current_();
}

const std::shared_ptr<manapi::event_loop> & manapi::async::eventloop() MANAPIHTTP_NOEXCEPT {
    return manapi::async::current()->eventloop();
}

const std::shared_ptr<manapi::timerpool> & manapi::async::etimerpool() MANAPIHTTP_NOEXCEPT {
    return manapi::async::current()->timerpool();
}

const std::shared_ptr<manapi::threadpool> & manapi::async::etaskpool() MANAPIHTTP_NOEXCEPT {
    return manapi::async::current()->etaskpool();
}

const manapi::async::shared_mthreadpool & manapi::async::mtaskpool() MANAPIHTTP_NOEXCEPT {
    return manapi::async::current()->threadpool();
}

const std::shared_ptr<manapi::logger> & manapi::async::log() MANAPIHTTP_NOEXCEPT {
    return manapi::async::current()->logger();
}

bool manapi::async::context_exists() MANAPIHTTP_NOEXCEPT {
    return manapi::async::internal::current_() != nullptr;
}

manapi::async::context::~context() = default;

manapi::status_or<std::shared_ptr<manapi::async::context>> manapi::async::context::create() MANAPIHTTP_NOEXCEPT {
    return async::context::create(std::thread::hardware_concurrency());
}

void manapi::async::internal::run_prepare_error_(std::exception_ptr err) MANAPIHTTP_NOEXCEPT {
    try {
        int errnum = manapi::ERR_OK;
        char errmsg[256];
        std::size_t errmsg_size = sizeof (errmsg);

        manapi::extract_exception_ptr(std::move(err), &errnum, errmsg, &errmsg_size);
        manapi_log_error("ctx: unhandled exception: %d, %.*s", errnum, errmsg_size, errmsg);
    }
    catch (std::exception const &ex) {
        manapi_log_error("%s due to %s", "logger failed", ex.what());
    }
}


void manapi::async::internal::run_prepare_std_exception_(std::exception const &e) MANAPIHTTP_NOEXCEPT {
    manapi_log_error("ctx: unhandled exception: %d, %s", ERR_UNKNOWN, e.what());
}


void manapi::async::internal::run_prepare_manapi_exception_(manapi::exception &e) MANAPIHTTP_NOEXCEPT {
    manapi_log_error("ctx: unhandled exception: %d, %s", static_cast<int>(e.err_num()), e.what());
}

const std::shared_ptr<manapi::threadpool> & manapi::async::internal::ethreadpool_(const shared_cthread &ctx) MANAPIHTTP_NOEXCEPT {
    return ctx->eventloop()->taskpool();
}


