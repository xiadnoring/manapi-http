#ifndef MANAPIBEFOREDELETE_HPP
#define MANAPIBEFOREDELETE_HPP

#include <functional>

namespace manapi::net::utils {
    class before_delete {
    public:
        explicit before_delete (const std::function <void()> &f);
        before_delete (before_delete &&n) noexcept;
        ~before_delete();
        before_delete &operator=(before_delete &&n) noexcept;
        void call ();
        void disable ();
        void enable ();
    private:
        bool autostart = true;
        std::function <void()> f;
    };
}

#endif //MANAPIBEFOREDELETE_HPP
