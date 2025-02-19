#pragma once

#include <functional>
#include "ManapiUtils.hpp"

namespace manapi {
    class before_delete {
    public:
        explicit before_delete (std::move_only_function <void()> f);
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
}