#ifndef MANAPIHTTP_MANAPIDECIMAL_H
#define MANAPIHTTP_MANAPIDECIMAL_H

#include <string>
#include <vector>
#include <iostream>
#include "ManapiUtils.h"
#include "gmp.h"

namespace manapi::utils {
    class decimal {
    public:
        decimal();
        ~decimal();

        explicit    decimal(const std::string   &num,           const size_t &precision = 128);
        explicit    decimal(const long long int &num,           const size_t &precision = 128);
        explicit    decimal(const ssize_t       &num,           const size_t &precision = 128);
        explicit    decimal(const double        &num,           const size_t &precision = 128);
        explicit    decimal(const unsigned long &num,           const size_t &precision = 128);
        explicit    decimal(const mpf_t         &num,           const size_t &precision = 128);

        decimal (const decimal &other);

        [[nodiscard]] std::string stringify () const;

        void        parse       (const size_t           &num);
        void        parse       (const std::string      &num);
        void        parse       (const long long int    &num);
        void        parse       (const ssize_t          &num);
        void        parse       (const double           &num);

        void        set_precision (const size_t &precision);

        decimal     operator/   (const decimal &oth) const;
        decimal     operator+   (const unsigned long &oth) const;
        decimal     operator+   (const decimal &oth) const;
        decimal     operator-   (const decimal &oth) const;
        decimal     operator-   (const unsigned long &oth) const;
        decimal     operator*   (const decimal &oth);
        void        operator-=  (const decimal &oth);
        void        operator+=  (const decimal &oth);
        void        operator*=  (const decimal &oth);
        void        operator/=  (const decimal &oth);
        bool        operator>   (const decimal &oth) const;
        bool        operator<   (const decimal &oth) const;
        bool        operator==  (const decimal &oth) const;
        bool        operator!=  (const decimal &oth) const;
        bool        operator>=  (const decimal &oth) const;
        bool        operator<=  (const decimal &oth) const;
        decimal&    operator--  ();
        decimal&    operator++  ();
        decimal     operator!   () const;
        decimal     operator-   () const;
        decimal&    operator=   (const decimal &oth);

    private:
        void cleanup();
        mpf_t   x;
    };
}

std::ostream &operator<<(std::ostream &os, const manapi::utils::decimal &m);
std::istream &operator>>(std::istream &is, manapi::utils::decimal &m);

#endif //MANAPIHTTP_MANAPIDECIMAL_H
