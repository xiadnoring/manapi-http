#include "ManapiApi.hpp"
#include "ManapiTaskFunction.hpp"

manapi::net::api::pool::pool(threadpool <task> *_task_pool): task_pool(_task_pool) {

}

manapi::net::api::pool::~pool() {

}

void manapi::net::api::pool::await(manapi::net::task *t) {
    t->doit();

    delete t;
}

void manapi::net::api::pool::async(manapi::net::task *t) {
    task_pool->append_task(t);
}

void manapi::net::api::pool::await(const std::function<void()> &func) {
    this->await(new function_task (func));
}

void manapi::net::api::pool::async(const std::function<void()> &func) {
    this->async(new function_task (func));
}
