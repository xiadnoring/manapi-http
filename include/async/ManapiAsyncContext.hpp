#pragma once

#include <memory>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"

namespace manapi::async {
    class context;
    class cthread;
    class mutex;
    class condition_variable;

    typedef std::shared_ptr<context> shared_ctx;
    typedef std::shared_ptr<threadpool<task>> shared_taskpool;
    typedef std::shared_ptr<logger> shared_logger;
    typedef std::shared_ptr<cthread> shared_cthread;

    typedef std::move_only_function<void(std::exception_ptr err)> run_cb;

    template<typename T>
    using run_cb_with_value = std::move_only_function<void(std::exception_ptr err, T *value)>;

    template<typename T = void>
    requires(std::is_same_v<T, void>)
    void run(manapi::future<> task, run_cb onfinish = nullptr);

    template<typename T = void>
    requires(std::is_same_v<T, void>)
    void run (auto && executor, run_cb onfinish = nullptr);

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run(manapi::future<T> task, run_cb_with_value<T> onfinish = nullptr);

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run (auto && executor, run_cb_with_value<T> onfinish = nullptr);
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
    typedef std::shared_ptr<timerpool> shared_timerpool;
    typedef std::shared_ptr<event_loop> shared_eventloop;
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
    class cthread {
    public:
        cthread (shared_eventloop eventloop, shared_taskpool taskpool, shared_timerpool timerpool, shared_logger logger);

        static void current (std::shared_ptr<cthread> thr);

        virtual ~cthread();

        [[nodiscard]] const shared_eventloop& eventloop();

        //[[nodiscard]] const shared_taskpool &taskpool();

        [[nodiscard]] const shared_timerpool &timerpool();

        [[nodiscard]] const shared_taskpool &etaskpool ();

        [[nodiscard]] const shared_logger &logger();

        //manapi::future<void> start ();

        virtual void sync_start ();

        void join ();

        virtual manapi::future<> stop ();
    protected:
        int flags;
        shared_eventloop eventloop_;
        shared_timerpool timerpool_;
        shared_taskpool taskpool_;
        shared_logger logger_;
    };

    typedef std::shared_ptr<cthread> shared_async_thread;

    class context : public cthread {
    public:
        context (shared_eventloop eventloop, std::shared_ptr<mthreadpool<task>> taskpool, shared_timerpool timerpool, shared_logger logger);

        ~context() override;

        // static void inloops (shared_ctx thr, std::function<void()> callback);

        static std::shared_ptr<context> create (unsigned int threadnum = std::thread::hardware_concurrency());

        static void run (shared_ctx ctx, int loops, std::function<void(std::function<void()> bind)> callback);

        static void threadpoolfs (std::size_t cnt = 4);

        static std::unique_ptr<manapi::sigset_t> blockedsignals ();

        const std::vector<shared_cthread> &loops ();

        static std::shared_ptr<context> gctx;
        static std::unique_ptr<manapi::sigset_t> gbs;
    private:
        std::vector<shared_cthread> loops_;
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
    manapi::future<T> blank_future () {
        co_return;
    }
}


namespace manapi::async::internal {
    void run_prepare_error_ (std::exception_ptr err);

    void run_prepare_std_exception_ (std::exception const &e);

    void run_prepare_manapi_exception_ (manapi::exception &e);

    template<typename T = void>
    requires(std::is_same_v<T, void>)
    void run_prepare_(std::unique_ptr<manapi::async::async_task_t<T>> task_data, run_cb onfinish) {
        if (onfinish) {
            task_data->task.onfinish([task = task_data.get(), onfinish = std::move(onfinish)] (std::exception_ptr err) mutable -> void {
                try {
                    onfinish(std::move(err));
                }
                catch (manapi::exception &e) {
                    run_prepare_manapi_exception_(e);
                }
                catch (std::exception const &e) {
                    run_prepare_std_exception_(e);
                }
                if (task->flags.exchange(1)) { delete task; }
            });
        }
        else {
            task_data->task.onfinish([task = task_data.get()] (std::exception_ptr err) mutable -> void {
                if (err)
                    run_prepare_error_(std::move(err));

                if (task->flags.exchange(1)) { delete task; }
            });
        }

        task_data->task();
        if (!task_data->flags.exchange(1)) {
            task_data.release();
        }
    }

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run_prepare_(std::unique_ptr<manapi::async::async_task_t<T>> task_data, run_cb_with_value<T> onfinish) {
        if (onfinish) {
            task_data->task.onfinish([task = task_data.get(), onfinish = std::move(onfinish)] (std::exception_ptr err, T *value) mutable -> void {
                try { onfinish(std::move(err), value); }
                catch (manapi::exception &e) { internal::run_prepare_manapi_exception_(e); }
                catch (std::exception const &e) { internal::run_prepare_std_exception_(e); }
                if (task->flags.exchange(1)) { delete task; }
            });
        }
        else {
            task_data->task.onfinish([task = task_data.get()] (std::exception_ptr err, T *value) mutable -> void {
                if (err) internal::run_prepare_error_(std::move(err));
                if (task->flags.exchange(1)) { delete task; }
            });
        }

        task_data->task();
        if (!task_data->flags.exchange(1)) {
            task_data.release();
        }
    }
}

namespace manapi::async {
    template<typename T>
    std::invoke_result_t <T> invoke (T executer) {
        co_return co_await executer();
    }

    template<typename T, typename ...Args>
    std::invoke_result_t <T, Args...> invoke (T executer, Args... args) {
        co_return co_await executer(std::move(args)...);
    }

    template<typename T>
    requires(std::is_same_v<T, void>)
    void run (auto && executor,  std::move_only_function<void(std::exception_ptr err)> onfinish ) {
        async::run<T> (manapi::async::invoke(std::forward<decltype(executor)>(executor)), std::move(onfinish));
    }

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run (auto &&executor, run_cb_with_value<T> onfinish) {
        async::run<T> (manapi::async::invoke(std::forward<decltype(executor)>(executor)), std::move(onfinish));
    }

    template<typename T>
    requires(!std::is_same_v<T, void>)
    void run(manapi::future<T> task, run_cb_with_value<T> onfinish) {
        auto task_data = std::make_unique<async_task_t<T>>(manapi::future<T>{task.release()});
        internal::run_prepare_<T>(std::move(task_data), std::move(onfinish));
    }

    template<typename T>
    requires(std::is_same_v<T, void>)
    void manapi::async::run(manapi::future<> task, std::move_only_function<void(std::exception_ptr err)> onfinish) {
        auto task_data = std::make_unique<async_task_t<T>>(manapi::future<T>{task.release()});
        internal::run_prepare_<T>(std::move(task_data), std::move(onfinish));
    }
}
