#ifndef MANAPIHTTP_MANAPIAPI_H
#define MANAPIHTTP_MANAPIAPI_H

#include "ManapiThreadPool.h"
#include "ManapiTask.h"

namespace manapi::net::api {
    class pool {
    public:
        explicit pool (threadpool<task> *_task_pool);
        ~pool ();

        void await (task *t);
        void async (task *t);

    private:
        threadpool<task> *task_pool;
    };
}

#endif