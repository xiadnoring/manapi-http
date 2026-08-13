#pragma once

#include <typeinfo>
#include <cstdint>

#include "../ManapiEventStructures.hpp"

#include "../ManapiErrors.hpp"

namespace manapi {
    class bytebuffer {
        struct slices_data {
            uv_buf_t *slices;
            int slices_cnt;
        };
        union buff_data {
            slices_data slices;
            uint8_t *src;
        };

    public:
        enum flags {
            BYTEBUFFER_FLAG_OBJECT_POOL = 1
        };

        bytebuffer ();

        bool operator==(const std::nullptr_t &) const;

        bytebuffer (void *src, std::size_t size);

        bytebuffer (void *src, std::size_t size, std::size_t reserved, uint8_t flags);

        static manapi::status_or<bytebuffer> create (std::size_t size);

        ~bytebuffer ();

        bytebuffer (bytebuffer &&n) MANAPIHTTP_NOEXCEPT;

        bytebuffer &operator=(bytebuffer &&n) MANAPIHTTP_NOEXCEPT;

        char *c_str ();

        char *data ();

        char &operator[] (std::size_t i_);

        char &at (std::size_t i_);

        MANAPIHTTP_NODISCARD const char *c_str () const;

        MANAPIHTTP_NODISCARD const char *data () const;

        operator bool () const;

        template<typename T>
        T*as() { return reinterpret_cast<T *> (this->src) + this->shift_; }

        MANAPIHTTP_NODISCARD std::size_t size () const;

        MANAPIHTTP_NODISCARD std::size_t realsize () const;

        manapi::status realresize (std::size_t s) MANAPIHTTP_NOEXCEPT;

        manapi::status resize (std::size_t s) MANAPIHTTP_NOEXCEPT;

        manapi::status resize_max (std::size_t s) MANAPIHTTP_NOEXCEPT;

        void remove_shift () MANAPIHTTP_NOEXCEPT;

        void clear () MANAPIHTTP_NOEXCEPT;

        void reinit ();

        void *release ();

        MANAPIHTTP_NODISCARD std::size_t shift () const;

        void shift (std::size_t n);

        void shift_add (std::size_t n);

        uint8_t flags ();

        MANAPIHTTP_NODISCARD bool empty () const;
    private:

        uint8_t flags_;
        uint32_t shift_;

        uint32_t s;
        uint32_t reserved;

        uint8_t *src;
    };
}