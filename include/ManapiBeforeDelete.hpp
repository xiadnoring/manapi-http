#pragma once

#include <functional>
#include "ManapiUtils.hpp"

namespace manapi {
    class before_delete {
    public:
        before_delete (nullptr_t);
        before_delete (std::move_only_function <void()> f);
        before_delete (before_delete &&n) noexcept;
        ~before_delete();
        before_delete &operator=(before_delete &&n) noexcept;
        void call ();
        void disable ();
        void enable ();
    private:
        bool autostart = true;
        std::move_only_function <void()> f;
    };

    class sbefore_delete {
    public:
        sbefore_delete (nullptr_t);
        sbefore_delete (std::move_only_function <void()> f);
        sbefore_delete (sbefore_delete &&n) noexcept;
        ~sbefore_delete();
        sbefore_delete &operator=(sbefore_delete &&n) noexcept;
        void call ();
    private:
        std::move_only_function <void()> f;
    };

    template<typename T, T v>
    class vbefore_delete {
    public:
        vbefore_delete (nullptr_t) {
            this->f = nullptr;
        }

        vbefore_delete (std::move_only_function <void(T)> f) {
            this->f = std::move(f);
        }

        ~vbefore_delete() {
            if (this->f) { auto cb = std::move(this->f); cb(v); }
        }

        void call (T n) {
            if (this->f) { auto cb = std::move(this->f); cb(std::move(n)); }
        }

        vbefore_delete (vbefore_delete &&n) noexcept = default;

        vbefore_delete &operator=(vbefore_delete &&n) noexcept = default;
    private:
        std::move_only_function <void(T)> f;
    };
}