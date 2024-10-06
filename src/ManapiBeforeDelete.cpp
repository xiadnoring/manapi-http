#include "ManapiBeforeDelete.hpp"

manapi::net::utils::before_delete::before_delete(const std::function<void()> &f) {
    this->f = f;
}

manapi::net::utils::before_delete::before_delete(before_delete &&n) noexcept {
    std::swap(this->f, n.f);
    std::swap(this->autostart, n.autostart);
}

manapi::net::utils::before_delete::~before_delete() {
    if (f != nullptr && autostart)
    {
        f();
    }
}

manapi::net::utils::before_delete & manapi::net::utils::before_delete::operator=(before_delete &&n) noexcept {
    std::swap(this->f, n.f);
    std::swap(this->autostart, n.autostart);
    return *this;
}

void manapi::net::utils::before_delete::call () {
    if (f != nullptr)
    {
        f();
    }

    disable();
}

void manapi::net::utils::before_delete::disable() {
    autostart = false;
}

void manapi::net::utils::before_delete::enable() {
    autostart = true;
}