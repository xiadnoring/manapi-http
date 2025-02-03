#include "ManapiBeforeDelete.hpp"

#include <utility>

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
        std::exchange(this->f, nullptr)();
    }
}

void manapi::before_delete::disable() {
    this->autostart = false;
}

void manapi::before_delete::enable() {
    this->autostart = true;
}