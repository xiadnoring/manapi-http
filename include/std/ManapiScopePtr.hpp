#pragma once

#include "../ManapiUtils.hpp"

namespace manapi {
    template<typename T>
    class scope_ptr {
    private:
        T* ptr;
        bool own;

    public:
        scope_ptr(T* p = nullptr, bool ownership = true)
            : ptr(p), own(ownership) {}

        ~scope_ptr() {
            if (this->own) {
                delete this->ptr;
            }
        }

        scope_ptr(const scope_ptr&) = delete;

        scope_ptr& operator=(const scope_ptr&) = delete;

        scope_ptr(scope_ptr&& other) MANAPIHTTP_NOEXCEPT
            : ptr(other.ptr), own(other.own) {
            other.ptr = nullptr;
            other.own = false;
        }

        scope_ptr& operator=(scope_ptr&& other) MANAPIHTTP_NOEXCEPT {
            if (this != &other) {
                auto ptrn = other.ptr;
                auto ownn = other.own;
                other.ptr = nullptr;
                other.own = false;
                if (this->own)
                    delete this->ptr;
                this->ptr = ptrn;
                this->own = ownn;
            }
            return *this;
        }

        void ownership(bool ownership) {
            this->own = ownership;
        }

        bool ownership() const {
            return this->own;
        }

        T *release () {
            this->own = false;
            return this->ptr;
        }

        T* get() const {
            return this->ptr;
        }

        T* operator->() const {
            return this->ptr;
        }

        T& operator*() const {
            return *this->ptr;
        }
    };
}