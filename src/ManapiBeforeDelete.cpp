#include "ManapiBeforeDelete.hpp"

manapi::before_delete::before_delete(const std::function<void()> &f) {
    this->f = f;
}

manapi::before_delete::before_delete(before_delete &&n) noexcept {
    std::swap(this->f, n.f);
    std::swap(this->autostart, n.autostart);
}

manapi::before_delete::~before_delete() {
    if (f != nullptr && autostart)
    {
        f();
    }
}

manapi::before_delete & manapi::before_delete::operator=(before_delete &&n) noexcept {
    std::swap(this->f, n.f);
    std::swap(this->autostart, n.autostart);
    return *this;
}

void manapi::before_delete::call () {
    if (f != nullptr)
    {
        f();
    }

    disable();
}

void manapi::before_delete::disable() {
    autostart = false;
}

void manapi::before_delete::enable() {
    autostart = true;
}