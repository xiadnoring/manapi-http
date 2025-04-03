#include "services/ManapiTaskFunction.hpp"

#include <utility>

manapi::function_task::function_task(std::move_only_function <void ()> func) {
    this->func = std::move(func);
}

manapi::function_task::function_task(function_task &&task) noexcept {
    this->func = std::move(task.func);
}

manapi::function_task & manapi::function_task::operator=(function_task &&task) noexcept {
    this->func = std::move(task.func);
    return *this;
}

void manapi::function_task::doit() {
    if (this->func) {
        this->func();
    }
}
