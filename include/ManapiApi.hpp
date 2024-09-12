#ifndef MANAPIHTTP_MANAPIAPI_H
#define MANAPIHTTP_MANAPIAPI_H

#include <functional>

#include "ManapiThreadPool.hpp"
#include "ManapiTask.hpp"

namespace manapi::net::api {
    class pool {
    public:
        explicit pool (threadpool<task> *_task_pool);
        ~pool ();

        void await (task *t);
        void async (task *t);

        void await (const std::function<void()> &func);
        void async (const std::function<void()> &func);

    private:
        threadpool<task> *task_pool;
    };
}

#endif