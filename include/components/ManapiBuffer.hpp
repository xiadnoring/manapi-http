#pragma once

#include <typeinfo>
#include <cstdint>

#include <uv.h>

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

        bytebuffer (void *src, std::size_t size, char flags);

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

        operator bool () const;

        template<typename T>
        T*as() { return reinterpret_cast<T *> (this->src) + this->shift_; }

        [[nodiscard]] std::size_t size () const;

        [[nodiscard]] std::size_t realsize () const;

        void realresize (std::size_t s);

        void resize (std::size_t s);

        void resize_max (std::size_t s);

        void clear ();

        void reinit ();

        void *release ();

        [[nodiscard]] std::size_t shift () const;

        void shift (std::size_t n);

        void shift_add (std::size_t n);

        uint8_t flags ();

        [[nodiscard]] bool empty () const;
    private:
        uint8_t flags_;
        uint32_t shift_;

        uint32_t s;
        uint32_t reserved;

        uint8_t *src;
    };
}