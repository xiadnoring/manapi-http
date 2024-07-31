#ifndef MANAPIHTTP_MANAPIBIGINT_H
#define MANAPIHTTP_MANAPIBIGINT_H

#include <string>
#include <vector>
#include <iostream>
#include "ManapiUtils.h"
#include "gmp.h"

namespace manapi::utils {
    class bigint {
    public:
        bigint();
        ~bigint();

        explicit    bigint(const std::string   &num,           const size_t &precision = 128);
        explicit    bigint(const std::wstring   &num,          const size_t &precision = 128);
        explicit    bigint(const long long int &num,           const size_t &precision = 128);
        explicit    bigint(const ssize_t       &num,           const size_t &precision = 128);
        explicit    bigint(const double        &num,           const size_t &precision = 128);
        explicit    bigint(const unsigned long &num,           const size_t &precision = 128);
        explicit    bigint(const mpf_t         &num,           const size_t &precision = 128);

        bigint (const bigint &other);

        [[nodiscard]] std::string stringify () const;

        void        parse       (const size_t           &num);
        void        parse       (const std::string      &num);
        void        parse       (const long long int    &num);
        void        parse       (const ssize_t          &num);
        void        parse       (const double           &num);

        void        set_precision (const size_t &precision);

        bigint     operator/   (const bigint &oth) const;
        bigint     operator+   (const unsigned long &oth) const;
        bigint     operator+   (const bigint &oth) const;
        bigint     operator-   (const bigint &oth) const;
        bigint     operator-   (const unsigned long &oth) const;
        bigint     operator*   (const bigint &oth);
        void        operator-=  (const bigint &oth);
        void        operator+=  (const bigint &oth);
        void        operator*=  (const bigint &oth);
        void        operator/=  (const bigint &oth);
        bool        operator>   (const bigint &oth) const;
        bool        operator<   (const bigint &oth) const;
        bool        operator==  (const bigint &oth) const;
        bool        operator!=  (const bigint &oth) const;
        bool        operator>=  (const bigint &oth) const;
        bool        operator<=  (const bigint &oth) const;
        bigint&    operator--  ();
        bigint&    operator++  ();
        bigint     operator!   () const;
        bigint     operator-   () const;
        bigint&    operator=   (const bigint &oth);

    private:
        void cleanup();
        mpf_t   x;
    };
}

std::ostream &operator<<(std::ostream &os, const manapi::utils::bigint &m);
std::istream &operator>>(std::istream &is, manapi::utils::bigint &m);

#endif //MANAPIHTTP_MANAPIBIGINT_H
