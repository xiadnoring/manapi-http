/**
 * @file ManapiBeforeDelete.hpp
 * @brief Before Delete Interfaces and Utilites
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <functional>
#include <type_traits>

#include "../ManapiDebug.hpp"
#include "../ManapiUtils.hpp"
#include "./ManapiFunction.hpp"

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

        before_delete (before_delete &&n) MANAPIHTTP_NOEXCEPT;

        ~before_delete();

        before_delete &operator=(before_delete &&n) MANAPIHTTP_NOEXCEPT;

        /**
         * Call the callback and remove it
         */
        void call () MANAPIHTTP_NOEXCEPT;

        /**
         * Disable the callback before destruction
         */
        void disable () MANAPIHTTP_NOEXCEPT;

        /**
         * Enable the callback before destruction
         */
        void enable () MANAPIHTTP_NOEXCEPT;
    private:
        /**
         * auto call state
         */
        bool active = true;

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

        sbefore_delete (sbefore_delete &&n) MANAPIHTTP_NOEXCEPT;

        ~sbefore_delete();

        sbefore_delete &operator=(sbefore_delete &&n) MANAPIHTTP_NOEXCEPT;

        /**
         * Call the callback and remove it
         */
        void call () MANAPIHTTP_NOEXCEPT;
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
            try {
                if (this->f) { auto cb = std::move(this->f); cb(v); }
            }
            catch (std::exception const &e) {
                manapi_log_error("%s due to %s", "vbefore_delete failed", e.what());
            }
        }

        /**
         * Call the callback and remove it
         */
        void call (T n) {
            try {
                if (this->f) { auto cb = std::move(this->f); cb(std::move(n)); }
            }
            catch (std::exception const &e) {
                manapi_log_error("%s due to %s", "vbefore_delete failed", e.what());
            }
        }

        vbefore_delete (vbefore_delete &&n) MANAPIHTTP_NOEXCEPT = default;

        vbefore_delete &operator=(vbefore_delete &&n) MANAPIHTTP_NOEXCEPT = default;
    private:
        std::move_only_function <void(T)> f;
    };

    template<std::size_t Size>
    class static_before_delete {
    public:
        /**
         * Initialize vbefore_delete with passing a callback
         * which is called before deconstruction
         *
         * @param f the callback which is called before deconstruction
         */
        static_before_delete (move_only_function_base<Size, void> f) {
            this->active = true;
            this->f = std::move(f);
        }

        ~static_before_delete() {
            try {
                if (this->f && this->active) { auto cb = std::move(this->f); cb(); }
            }
            catch (std::exception const &e) {
                manapi_log_error("%s due to %s", "static_before_delete failed", e.what());
            }
        }

        /**
         * Call the callback and remove it
         */
        void call () {
            try {
                if (this->f) { auto cb = std::move(this->f); cb(); }
            }
            catch (std::exception const &e) {
                manapi_log_error("%s due to %s", "static_before_delete failed", e.what());
            }
        }

        static_before_delete (static_before_delete &&n) MANAPIHTTP_NOEXCEPT {
            this->active = std::exchange(n.active, true);
            this->f = std::move(n.f);
        }

        static_before_delete &operator=(static_before_delete &&n) MANAPIHTTP_NOEXCEPT {
            if (this != &n) {
                this->active = std::exchange(n.active, true);
                this->f = std::move(n.f);
            }
            return *this;
        }
    private:
        move_only_function_base<Size, void> f;
        bool active;
    };
}