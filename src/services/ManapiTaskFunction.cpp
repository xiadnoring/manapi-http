#include "services/ManapiTaskFunction.hpp"

manapi::net::function_task::function_task(const std::function <void ()> &func) {
    this->func = func;
}

manapi::net::function_task::function_task(function_task &&task) noexcept {
    this->func = std::move(task.func);
}

manapi::net::function_task & manapi::net::function_task::operator=(function_task &&task) noexcept {
    this->func = std::move(task.func);
    return *this;
}

void manapi::net::function_task::doit() {
    if (func != nullptr) {
        func ();
    }
}
