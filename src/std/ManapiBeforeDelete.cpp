#include <utility>

#include "ManapiUtils.hpp"
#include "std/ManapiBeforeDelete.hpp"

manapi::before_delete::before_delete(std::move_only_function<void()> f) {
    this->f = std::move(f);
}

manapi::before_delete::before_delete(before_delete &&n) noexcept {
    std::swap(this->f, n.f);
    std::swap(this->autostart, n.autostart);
}

manapi::before_delete::~before_delete() {
    try {
        if (this->f && this->autostart)
            this->f();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "before_delete failed", e.what());
    }
}

manapi::before_delete & manapi::before_delete::operator=(before_delete &&n) noexcept {
    std::swap(this->f, n.f);
    std::swap(this->autostart, n.autostart);
    return *this;
}

void manapi::before_delete::call () MANAPIHTTP_NOEXCEPT {
    try {
        this->disable();

        if (this->f) {
            auto cb = std::move(this->f);
            cb();
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "before_delete failed", e.what());
    }
}

void manapi::before_delete::disable() MANAPIHTTP_NOEXCEPT {
    this->autostart = false;
}

void manapi::before_delete::enable() MANAPIHTTP_NOEXCEPT {
    this->autostart = true;
}

manapi::sbefore_delete::sbefore_delete(std::move_only_function<void()> f) {
    this->f = std::move(f);
}

manapi::sbefore_delete::sbefore_delete(sbefore_delete &&n) noexcept = default;

manapi::sbefore_delete::~sbefore_delete() {
    try {
        if (this->f) {
            auto cb = std::move(this->f);
            cb();
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "sbefore_delete failed", e.what());
    }
}

manapi::sbefore_delete & manapi::sbefore_delete::operator=(sbefore_delete &&n) noexcept = default;

void manapi::sbefore_delete::call() MANAPIHTTP_NOEXCEPT {
    try {
        if (this->f) {
            auto cb = std::move(this->f);
            cb();
        }
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "sbefore_delete failed", e.what());
    }
}

