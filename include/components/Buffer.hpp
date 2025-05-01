#pragma once

#include <typeinfo>
#include <cstdint>

namespace manapi {
    class bytebuffer {
    public:
        bytebuffer ();
        bytebuffer (void *src, std::size_t size);
        bytebuffer (std::size_t size);
        ~bytebuffer ();
        bytebuffer (bytebuffer &&n) noexcept;
        bytebuffer &operator=(bytebuffer &&n) noexcept;
        char *c_str ();
        char *data ();
        char &operator[] (std::size_t i_);
        char &at (std::size_t i_);
        [[nodiscard]] const char *c_str () const;
        [[nodiscard]] const char *data () const;
        template<typename T>
        T*as() { return reinterpret_cast<T *> (this->src); }
        [[nodiscard]] std::size_t size () const;
        [[nodiscard]] std::size_t realsize () const;
        void resize (std::size_t s);
        void clear ();
        void reinit ();
        void *release ();
        [[nodiscard]] bool empty () const;
    private:
        uint8_t *src{nullptr};
        std::size_t s{0};
        std::size_t reserved{0};
    };
}