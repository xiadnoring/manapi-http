#include "ManapiBeforeDelete.hpp"
#include "include/ManapiUtils.hpp"
#include <utility>

manapi::before_delete::before_delete(nullptr_t) {
    this->f = nullptr;
}

manapi::before_delete::before_delete(std::move_only_function<void()> f) {
    this->f = std::move(f);
}

manapi::before_delete::before_delete(before_delete &&n) noexcept {
    std::swap(this->f, n.f);
    std::swap(this->autostart, n.autostart);
}

manapi::before_delete::~before_delete() {
    if (this->f && this->autostart)
    {
        this->f();
    }
}

manapi::before_delete & manapi::before_delete::operator=(before_delete &&n) noexcept {
    std::swap(this->f, n.f);
    std::swap(this->autostart, n.autostart);
    return *this;
}

void manapi::before_delete::call () {
    this->disable();

    if (this->f) {
        auto cb = std::move(this->f);
        cb();
    }
}

void manapi::before_delete::disable() {
    this->autostart = false;
}

void manapi::before_delete::enable() {
    this->autostart = true;
}

manapi::sbefore_delete::sbefore_delete(nullptr_t) {
    this->f = nullptr;
}

manapi::sbefore_delete::sbefore_delete(std::move_only_function<void()> f) {
    this->f = std::move(f);
}

manapi::sbefore_delete::sbefore_delete(sbefore_delete &&n) noexcept = default;

manapi::sbefore_delete::~sbefore_delete() {
    if (this->f) {
        auto cb = std::move(this->f);
        cb();
    }
}

manapi::sbefore_delete & manapi::sbefore_delete::operator=(sbefore_delete &&n) noexcept = default;

void manapi::sbefore_delete::call() {
    if (this->f) {
        auto cb = std::move(this->f);
        cb();
    }
}

