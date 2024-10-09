#include "ManapiApi.hpp"
#include "ManapiTaskFunction.hpp"

manapi::net::api::pool::pool(threadpool <task> *_task_pool): task_pool(_task_pool) {

}

manapi::net::api::pool::~pool() {

}

void manapi::net::api::pool::await(std::unique_ptr<manapi::net::task> t) {
    t->doit();
}

void manapi::net::api::pool::async(std::unique_ptr<manapi::net::task> t) {
    task_pool->append_task(std::move(t));
}

void manapi::net::api::pool::await(const std::function<void()> &func) {
    this->await(std::move(std::make_unique<function_task>(func)));
}

void manapi::net::api::pool::async(const std::function<void()> &func) {
    this->async(std::move(std::make_unique<function_task>(func)));
}
