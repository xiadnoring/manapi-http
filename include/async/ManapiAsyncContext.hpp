#pragma once

#include <memory>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"

namespace manapi::async {
    class context;
    class mutex;
    class condition_variable;

    typedef std::shared_ptr<context> shared_ctx;

    void run(const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<> task, std::move_only_function<void()> onfinish = nullptr);
    void run (const std::shared_ptr<context> &ctx, manapi::future<> task,  std::move_only_function<void()> onfinish = nullptr);
    void run (const std::shared_ptr<context> &ctx, auto && executor,  std::move_only_function<void()> onfinish = nullptr);
    void run (const std::shared_ptr<threadpool<task>> &taskpool, auto && executor,  std::move_only_function<void()> onfinish = nullptr);
    const std::shared_ptr<threadpool<task>> &as_threadpool(const std::shared_ptr<context> &ctx);
}

namespace manapi {
    class event_loop;
    class timerpool;
}

#define GCTX(...) manapi::async::context::gctx, __VA_ARGS__
#define GCTX_OBJ manapi::async::context::gctx

#include "../services/ManapiEventLoop.hpp"
#include "../services/ManapiTimerPool.hpp"
#include "./ManapiAsyncLogger.hpp"

namespace manapi::async {
    class context {
    public:
        context (std::shared_ptr<event_loop> watcher, std::shared_ptr<threadpool<task>> taskpool, std::shared_ptr<manapi::timerpool> timerpool, std::shared_ptr<manapi::logger> logger);

        static std::shared_ptr<context> create (const unsigned int &threadnum = std::thread::hardware_concurrency(), const ssize_t &timer_delay = 60);

        manapi::future<void> start ();
        void sync_start();
        manapi::future<void> stop ();

        void join ();

        [[nodiscard]] const std::shared_ptr<event_loop>& eventloop();
        [[nodiscard]] const std::shared_ptr<threadpool<task>> &taskpool();
        [[nodiscard]] const std::shared_ptr<manapi::timerpool> &timerpool();
        [[nodiscard]] const std::shared_ptr<manapi::logger> &logger();

        ~context();

        static std::shared_ptr<context> gctx;
    private:
        std::weak_ptr<context> weak;
        std::shared_ptr<event_loop> watcher_;
        std::shared_ptr<threadpool<task>> taskpool_;
        std::shared_ptr<manapi::timerpool> timerpool_;
        std::shared_ptr<manapi::logger> logger_;
    };


    struct async_task_t {
        manapi::future<void> task;
    };
#if defined(MANAPIHTTP_ASYNC_DEBUG)
    inline std::mutex async_tasks_mx;
    inline std::map <size_t, async_task_t*> async_tasks;
#endif
    void run_prepare_ (const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<> &task, std::unique_ptr<async_task_t> task_data, std::move_only_function<void()> onfinish);
    manapi::future<> blank_future();

    template<typename T>
    std::invoke_result_t <T> invoke (T &&executer) {
        auto cb (std::forward<decltype(executer)>(executer));
        co_return co_await cb();
    }

    template<typename T, typename ...Args>
    std::invoke_result_t <T, Args...> invoke (T &&executer, Args &&...args) {
        auto cb (std::forward<decltype(executer)>(executer));
        co_return co_await cb(args...);
    }

    void run (const std::shared_ptr<context> &ctx, auto && executor,  std::move_only_function<void()> onfinish ) {
        async::run (ctx->taskpool(), invoke(std::forward<decltype(executor)>(executor)), std::move(onfinish));
    }

    void run (const std::shared_ptr<threadpool<task>> &taskpool, auto && executor,  std::move_only_function<void()> onfinish ) {
        async::run (taskpool, invoke(std::forward<decltype(executor)>(executor)), std::move(onfinish));
    }
}
