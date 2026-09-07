#pragma once

#include "./../ManapiUtils.hpp"

#include <functional>

namespace manapi {
    enum stoken_flags {
        STOKEN_FLAG_IMMEDIATELY_CALL = 1<<0
    };
    class stoken {
    public:
        struct data_t;

        stoken ( std::move_only_function<void()> fin_cb, int flags = 0 );

        stoken ();

        ~stoken ();

        stoken (stoken &&n) MANAPIHTTP_NOEXCEPT;

        stoken &operator= (stoken &&n) MANAPIHTTP_NOEXCEPT;

        stoken (const stoken &n) MANAPIHTTP_NOEXCEPT;

        stoken &operator= (const stoken &n) MANAPIHTTP_NOEXCEPT;

        stoken* next ( const manapi::stoken &token ) MANAPIHTTP_NOEXCEPT;

        int ref () MANAPIHTTP_NOEXCEPT;

        void unref () MANAPIHTTP_NOEXCEPT;

        MANAPIHTTP_NODISCARD std::size_t count () const MANAPIHTTP_NOEXCEPT;

        void reset () MANAPIHTTP_NOEXCEPT;

        void clear () MANAPIHTTP_NOEXCEPT;

        operator bool () const MANAPIHTTP_NOEXCEPT;
    private:
        data_t *m_data;
    };
}