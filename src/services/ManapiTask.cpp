//
// Created by Timur on 19.05.2024.
//

#include "services/ManapiTask.hpp"
#include "ManapiUtils.hpp"

manapi::net::task::task() {}

manapi::net::task::task(task &&n) noexcept {}

manapi::net::task & manapi::net::task::operator=(task &&n) noexcept {
    return *this;
}

manapi::net::task::~task() {}

void manapi::net::task::doit() {}

void manapi::net::task::stop() {}

