//
// Created by Timur on 19.05.2024.
//

#include "ManapiTask.h"
#include "ManapiUtils.h"

manapi::net::task::task() {}

manapi::net::task::~task() {}

void manapi::net::task::doit() {}

void manapi::net::task::stop() {
    throw manapi::utils::manapi_exception ("stop task");
}

