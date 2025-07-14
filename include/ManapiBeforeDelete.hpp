/**
 * @file ManapiBeforeDelete.hpp
 * @brief Before Delete Interfaces and Utilites
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <functional>
#include "ManapiUtils.hpp"

namespace manapi {
    class before_delete {
    public:

        /**
         * Initialize before_delete with passing a callback
         * which is called before deconstruction
         *
         * @param f the callback which is called before deconstruction
         */
        before_delete (std::move_only_function <void()> f);

        before_delete (before_delete &&n) noexcept;

        ~before_delete();

        before_delete &operator=(before_delete &&n) noexcept;

        /**
         * Call the callback and remove it
         */
        void call ();

        /**
         * Disable the callback before destruction
         */
        void disable ();

        /**
         * Enable the callback before destruction
         */
        void enable ();
    private:
        /**
         * auto call state
         */
        bool autostart = true;

        std::move_only_function <void()> f;
    };

    class sbefore_delete {
    public:
        /**
         * Initialize sbefore_delete with passing a callback
         * which is called before deconstruction
         *
         * @param f the callback which is called before deconstruction
         */
        sbefore_delete (std::move_only_function <void()> f);

        sbefore_delete (sbefore_delete &&n) noexcept;

        ~sbefore_delete();

        sbefore_delete &operator=(sbefore_delete &&n) noexcept;

        /**
         * Call the callback and remove it
         */
        void call ();
    private:
        std::move_only_function <void()> f;
    };

    template<typename T, T v>
    class vbefore_delete {
    public:
        /**
         * Initialize vbefore_delete with passing a callback
         * which is called before deconstruction
         *
         * @param f the callback which is called before deconstruction
         */
        vbefore_delete (std::move_only_function <void(T)> f) {
            this->f = std::move(f);
        }

        ~vbefore_delete() {
            if (this->f) { auto cb = std::move(this->f); cb(v); }
        }

        /**
         * Call the callback and remove it
         */
        void call (T n) {
            if (this->f) { auto cb = std::move(this->f); cb(std::move(n)); }
        }

        vbefore_delete (vbefore_delete &&n) noexcept = default;

        vbefore_delete &operator=(vbefore_delete &&n) noexcept = default;
    private:
        std::move_only_function <void(T)> f;
    };
}