#include "services/ManapiTaskFunction.hpp"

#include <utility>

manapi::net::function_task::function_task(std::move_only_function <void ()> func) {
    this->func = std::move(func);
}

manapi::net::function_task::function_task(function_task &&task) noexcept {
    this->func = std::move(task.func);
}

manapi::net::function_task & manapi::net::function_task::operator=(function_task &&task) noexcept {
    this->func = std::move(task.func);
    return *this;
}

void manapi::net::function_task::doit() {
    if (this->func) {
        auto cb = std::move(this->func);
        cb();
    }
}
