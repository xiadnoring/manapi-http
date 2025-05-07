#pragma once

#include <memory>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"

namespace manapi::async {
    class context;
    class mutex;
    class condition_variable;

    typedef std::move_only_function<void(std::exception_ptr err)> run_cb;

    template<typename T>
    using run_cb_with_value = std::move_only_function<void(std::exception_ptr err, T *value)>;

    template<typename T = void>
    requires(std::is_same_v<T, void>)
    void run(const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<> task, run_cb onfinish = nullptr);

    template<typename T = void>
    requires(std::is_same_v<T, void>)
    void run (const std::shared_ptr<context> &ctx, manapi::future<> task, run_cb onfinish = nullptr);

    template<typename T = void>
    requires(std::is_same_v<T, void>)
    void run (const std::shared_ptr<context> &ctx, auto && executor, run_cb onfinish = nullptr);

    template<typename T = void>
    requires(std::is_same_v<T, void>)
    void run (const std::shared_ptr<threadpool<task>> &taskpool, auto && executor, run_cb onfinish = nullptr);

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run(const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<T> task, run_cb_with_value<T> onfinish = nullptr);

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run (const std::shared_ptr<context> &ctx, manapi::future<T> task, run_cb_with_value<T> onfinish = nullptr);

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run (const std::shared_ptr<context> &ctx, auto && executor, run_cb_with_value<T> onfinish = nullptr);

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run (const std::shared_ptr<threadpool<task>> &taskpool, auto && executor, run_cb_with_value<T> onfinish = nullptr);
}

namespace manapi::async::internal {
    const std::shared_ptr<threadpool<task>> &as_threadpool(const std::shared_ptr<context> &ctx);
}

namespace manapi {
#if defined (__unix__) || defined(__APPLE__)
    struct sigset_t : public ::sigset_t {};
#else
    struct sigset_t {
        char payload[1];
    };
#endif

    class event_loop;
    class timerpool;
}

namespace manapi::async {
    typedef std::shared_ptr<context> shared_ctx;
    typedef std::shared_ptr<threadpool<task>> shared_taskpool;
    typedef std::shared_ptr<timerpool> shared_timerpool;
    typedef std::shared_ptr<event_loop> shared_eventloop;
    typedef std::shared_ptr<logger> shared_logger;
}

#define GCTX(...) manapi::async::context::gctx, __VA_ARGS__
#define GCTX_OBJ manapi::async::context::gctx

#include "../services/ManapiEventLoop.hpp"
#include "../services/ManapiTimerPool.hpp"
#include "./ManapiAsyncLogger.hpp"

#if defined (__unix__) || defined(__APPLE__)
#   include <pthread.h>
#endif

namespace manapi::async {
    struct async_thread_t {
        shared_eventloop eventloop;
        shared_timerpool timerpool;
    };

    typedef std::shared_ptr<async_thread_t> shared_async_thread;

    class context {
    public:
        context (shared_async_thread main, std::shared_ptr<mthreadpool<task>> taskpool, shared_logger logger);

        static std::shared_ptr<context> create (unsigned int threadnum = std::thread::hardware_concurrency(), ssize_t timer_delay = 60);

        static void threadpoolfs (std::size_t cnt = 4);

        static std::unique_ptr<manapi::sigset_t> blockedsignals ();

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
        static std::unique_ptr<manapi::sigset_t> gbs;
    private:
        std::weak_ptr<context> weak;
        std::shared_ptr<mthreadpool<task>> taskpool_;
        std::shared_ptr<manapi::logger> logger_;
        shared_async_thread main_;
    };


    template<typename T = void>
    struct async_task_t {
        manapi::future<T> task;
        std::atomic<char> flags;
    };

    template<typename T>
    requires(!std::is_same_v<T, void>)
    manapi::future<T> blank_future(T &&n) {
        co_return std::forward<decltype(n)>(n);
    }

    template<typename T>
    requires(std::is_same_v<T, void>)
    manapi::future<T> manapi::async::blank_future () {
        co_return;
    }
}


namespace manapi::async::internal {
    void run_prepare_error_ (const shared_taskpool &taskpool, std::exception_ptr err);

    void run_prepare_std_exception_ (const shared_taskpool &taskpool, std::exception const &e);

    void run_prepare_manapi_exception_ (const shared_taskpool &taskpool, manapi::exception &e);

    template<typename T = void>
    requires(std::is_same_v<T, void>)
    void run_prepare_(const shared_taskpool &taskpool, std::unique_ptr<manapi::async::async_task_t<T>> task_data, run_cb onfinish) {

        if (onfinish) {
            task_data->task.onfinish([taskpool, task = task_data.get(), onfinish = std::move(onfinish)] (std::exception_ptr err) mutable -> void {
                try {
                    onfinish(std::move(err));
                }
                catch (manapi::exception &e) {
                    run_prepare_manapi_exception_(taskpool, e);
                }
                catch (std::exception const &e) {
                    run_prepare_std_exception_(taskpool, e);
                }
                if (task->flags.exchange(1)) { delete task; }
            }, taskpool.get());
        }
        else {
            task_data->task.onfinish([taskpool, task = task_data.get()] (std::exception_ptr err) mutable -> void {
                if (err)
                    run_prepare_error_(taskpool, std::move(err));

                if (task->flags.exchange(1)) { delete task; }
            }, taskpool.get());
        }

        task_data->task();
        if (!task_data->flags.exchange(1)) {
            task_data.release();
        }
    }

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run_prepare_(const shared_taskpool &taskpool, std::unique_ptr<manapi::async::async_task_t<T>> task_data, run_cb_with_value<T> onfinish) {

        if (onfinish) {
            task_data->task.onfinish([taskpool, task = task_data.get(), onfinish = std::move(onfinish)] (std::exception_ptr err, T *value) mutable -> void {
                try { onfinish(std::move(err), value); }
                catch (manapi::exception &e) { internal::run_prepare_manapi_exception_(taskpool, e); }
                catch (std::exception const &e) { internal::run_prepare_std_exception_(taskpool, e); }
                if (task->flags.exchange(1)) { delete task; }
            }, taskpool.get());
        }
        else {
            task_data->task.onfinish([taskpool, task = task_data.get()] (std::exception_ptr err, T *value) mutable -> void {
                if (err) internal::run_prepare_error_(taskpool, std::move(err));
                if (task->flags.exchange(1)) { delete task; }
            }, taskpool.get());
        }

        task_data->task();
        if (!task_data->flags.exchange(1)) {
            task_data.release();
        }
    }
}

namespace manapi::async {
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

    template<typename T>
    requires(std::is_same_v<T, void>)
    void run (const std::shared_ptr<context> &ctx, auto && executor,  std::move_only_function<void(std::exception_ptr err)> onfinish ) {
        async::run<T> (ctx->taskpool(), invoke(std::forward<decltype(executor)>(executor)), std::move(onfinish));
    }

    template<typename T>
    requires(std::is_same_v<T, void>)
    void run (const shared_taskpool &taskpool, auto && executor,  std::move_only_function<void(std::exception_ptr err)> onfinish ) {
        async::run<T> (taskpool, invoke(std::forward<decltype(executor)>(executor)), std::move(onfinish));
    }

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run (const shared_ctx &ctx, auto &&executor, run_cb_with_value<T> onfinish) {
        async::run<T> (ctx, invoke(std::forward<decltype(executor)>(executor)), std::move(onfinish));
    }

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run (const shared_taskpool &taskpool, auto &&executor, run_cb_with_value<T> onfinish) {
        async::run<T> (taskpool, invoke(std::forward<decltype(executor)>(executor)), std::move(onfinish));
    }

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run(const shared_taskpool &taskpool, manapi::future<T> task, run_cb_with_value<T> onfinish) {
        auto task_data = std::make_unique<async_task_t<T>>(manapi::future<T>{task.release()});
        internal::run_prepare_<T>(taskpool, std::move(task_data), std::move(onfinish));
    }

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run (const shared_ctx &ctx, manapi::future<T> task, run_cb_with_value<T> onfinish) {
        async::run<T> (ctx->taskpool(), std::move(task), std::move(onfinish));
    }

    template<typename T>
    requires(std::is_same_v<T, void>)
    void manapi::async::run(const std::shared_ptr<context> &ctx, manapi::future<> task,std::move_only_function<void(std::exception_ptr err)> onfinish) {
        async::run<T> (ctx->taskpool(), std::move(task), std::move(onfinish));
    }

    template<typename T>
    requires(std::is_same_v<T, void>)
    void manapi::async::run(const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<> task, std::move_only_function<void(std::exception_ptr err)> onfinish) {
        auto task_data = std::make_unique<async_task_t<T>>(manapi::future<T>{task.release()});
        internal::run_prepare_<T>(taskpool, std::move(task_data), std::move(onfinish));
    }
}
