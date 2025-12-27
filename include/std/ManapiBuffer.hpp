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

        bytebuffer (void *src, uint32_t size);

        bytebuffer (void *src, uint32_t size, char flags);

        static manapi::status_or<bytebuffer> create (uint32_t size);

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

        MANAPIHTTP_NODISCARD uint32_t size () const;

        MANAPIHTTP_NODISCARD uint32_t realsize () const;

        manapi::status realresize (uint32_t s) MANAPIHTTP_NOEXCEPT;

        manapi::status resize (uint32_t s) MANAPIHTTP_NOEXCEPT;

        manapi::status resize_max (uint32_t s) MANAPIHTTP_NOEXCEPT;

        void remove_shift () MANAPIHTTP_NOEXCEPT;

        void clear () MANAPIHTTP_NOEXCEPT;

        void reinit ();

        void *release ();

        MANAPIHTTP_NODISCARD uint32_t shift () const;

        void shift (uint32_t n);

        void shift_add (uint32_t n);

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