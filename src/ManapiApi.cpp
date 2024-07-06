#include "ManapiApi.h"

manapi::net::api::pool::pool(threadpool <task> *_task_pool): task_pool(_task_pool) {

}

manapi::net::api::pool::~pool() {

}

void manapi::net::api::pool::await(manapi::net::task *t) {
    t->doit();
}

void manapi::net::api::pool::async(manapi::net::task *t) {
    task_pool->append_task(t);
}
