#include "ManapiTaskFunction.hpp"

manapi::net::function_task::function_task(const std::function <void ()> &_func) {
    func = _func;
}

void manapi::net::function_task::doit() {
    func ();
}
