#pragma once

#include <memory>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"

namespace manapi::async {
    class context;
    class mutex;
    class condition_variable;

    inline void run(const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<> task, std::function<void()> onfinish = nullptr);
    inline void run (const std::shared_ptr<context> &ctx, manapi::future<> task,  std::function<void()> onfinish = nullptr);
    inline void run (const std::shared_ptr<context> &ctx, auto && executor,  std::function<void()> onfinish = nullptr);
    inline void run (const std::shared_ptr<threadpool<task>> &taskpool, auto && executor,  std::function<void()> onfinish = nullptr);
    inline const std::shared_ptr<threadpool<task>> &as_threadpool(const std::shared_ptr<context> &ctx);
}

namespace manapi {
    class event_loop;
    class timerpool;
}

#include "../services/ManapiEventLoop.hpp"
#include "../services/ManapiTimerPool.hpp"

namespace manapi::async {
    class context {
    public:
        context (std::shared_ptr<event_loop> watcher, std::shared_ptr<threadpool<task>> taskpool, std::shared_ptr<manapi::timerpool> timerpool);

        static std::shared_ptr<context> create (const unsigned int &threadnum = std::thread::hardware_concurrency(), const double &timer_delay = 0.2);

        manapi::future<void> start ();
        void sync_start();
        manapi::future<void> stop ();

        void join ();

        [[nodiscard]] const std::shared_ptr<event_loop>& eventloop();
        [[nodiscard]] const std::shared_ptr<threadpool<task>> &taskpool();
        [[nodiscard]] const std::shared_ptr<manapi::timerpool> &timerpool();

        ~context();
    private:
        std::weak_ptr<context> weak;
        std::shared_ptr<event_loop> watcher_;
        std::shared_ptr<threadpool<task>> taskpool_;
        std::shared_ptr<manapi::timerpool> timerpool_;
    };

    inline std::mutex async_tasks_mx;

    struct async_task_t {
        manapi::future<void> task;
    };

    inline std::map <size_t, std::shared_ptr<async_task_t>> async_tasks;

    inline size_t _run_prepare (const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<> &task, std::function<void()> onfinish) {
        auto index = reinterpret_cast <size_t> (task.get_handle().address());

        task.on_finish([index, taskpool, onfinish = std::move(onfinish)] () mutable -> void {
            if (onfinish) {
                taskpool->append_task(std::move(onfinish));
            }

            decltype(async_tasks)::node_type data;
            {
                std::lock_guard<std::mutex> lk (async_tasks_mx);
                auto it = async_tasks.find(index);
                if (it != async_tasks.end()) {
                    data = std::move(async_tasks.extract(it));
                }
            }
        }, taskpool);

        task();

        return index;
    }

    template<typename T>
    std::invoke_result_t <T> invoke (T executer) {
        auto cb (std::forward<decltype(executer)>(executer));
        co_return co_await cb();
    }

    template<typename T, typename ...Args>
    std::invoke_result_t <T> invoke (T executer, Args &&...args) {
        auto cb (std::forward<decltype(executer)>(executer));
        co_return co_await cb(args...);
    }

    inline void run(const std::shared_ptr<threadpool<task>> &taskpool, manapi::future<> task, std::function<void()> onfinish) {
        const size_t index = async::_run_prepare(taskpool, task, std::move(onfinish));

        if (!task.finished()) {
            std::lock_guard<std::mutex> lk (async_tasks_mx);
            async_tasks.insert({index, std::make_shared<async_task_t>(std::move(task))});
        }
    }

    inline void run (const std::shared_ptr<context> &ctx, manapi::future<> task,  std::function<void()> onfinish ) {
        async::run (ctx->taskpool(), std::move(task), std::move(onfinish));
    }

    inline const std::shared_ptr<threadpool<task>> & as_threadpool(const std::shared_ptr<context> &ctx) {
        return ctx->taskpool();
    }

    inline void run (const std::shared_ptr<context> &ctx, auto && executor,  std::function<void()> onfinish ) {
        async::run (ctx->taskpool(), invoke(std::forward<decltype(executor)>(executor)), std::move(onfinish));
    }

    inline void run (const std::shared_ptr<threadpool<task>> &taskpool, auto && executor,  std::function<void()> onfinish ) {
        async::run (taskpool, invoke(std::forward<decltype(executor)>(executor)), std::move(onfinish));
    }
}
