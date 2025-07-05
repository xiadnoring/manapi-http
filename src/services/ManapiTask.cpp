//
// Created by Timur on 19.05.2024.
//

#include "services/ManapiTask.hpp"
#include "../include/ManapiUtils.hpp"

manapi::task::task() {}

manapi::task::task(task &&n) noexcept {}

manapi::task & manapi::task::operator=(task &&n) noexcept {
    return *this;
}

manapi::task::~task() {}

void manapi::task::doit() {}

void manapi::task::stop() {}

