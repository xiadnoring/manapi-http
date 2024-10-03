#ifndef MANAPIHTTP_MANAPIBIGINT_H
#define MANAPIHTTP_MANAPIBIGINT_H

#include <string>
#include <vector>
#include <iostream>
#include "gmp.h"

namespace manapi {
    class bigint {
    public:
        bigint();
        ~bigint();

        explicit    bigint(const std::string   &num,           const size_t &precision = 128);
        explicit    bigint(const std::wstring   &num,          const size_t &precision = 128);
        explicit    bigint(const long long int &num,           const size_t &precision = 128);
        explicit    bigint(const ssize_t       &num,           const size_t &precision = 128);
        explicit    bigint(const int            &num,           const size_t &precision = 128);
        explicit    bigint(const double        &num,           const size_t &precision = 128);
        explicit    bigint(const long double   &num,           const size_t &precision = 128);
        explicit    bigint(const unsigned long &num,           const size_t &precision = 128);
        explicit    bigint(const mpf_t         &num,           const size_t &precision = 128);

        bigint(bigint &&other) noexcept;
        bigint (const bigint &other);

        [[nodiscard]] std::string stringify () const;
        [[nodiscard]] ssize_t numberify () const;
        [[nodiscard]] double decimalify () const;

        void        parse       (const size_t           &num);
        void        parse       (const std::string      &num);
        void        parse       (const long long int    &num);
        void        parse       (const ssize_t          &num);
        void        parse       (const double           &num);
        void        parse       (const long double      &num);

        void        set_precision (const size_t &precision);
        [[nodiscard]] size_t      get_precision () const;

        bigint     operator/   (const bigint &oth) const;
        bigint     operator/   (const int &oth) const;
        bigint     operator/   (const ssize_t &oth) const;
        bigint     operator/   (const size_t &oth) const;
        bigint     operator/   (const double &oth) const;
        bigint     operator/   (const long double &oth) const;
        bigint     operator+   (const ssize_t &oth) const;
        bigint     operator+   (const size_t &oth) const;
        bigint     operator+   (const int &oth) const;
        bigint     operator+   (const bigint &oth) const;
        bigint     operator-   (const bigint &oth) const;
        bigint     operator-   (const int &oth) const;
        bigint     operator-   (const ssize_t &oth) const;
        bigint     operator-   (const size_t &oth) const;
        bigint     operator-   (const double &oth) const;
        bigint     operator+   (const double &oth) const;
        bigint     operator-   (const long double &oth) const;
        bigint     operator+   (const long double &oth) const;
        bigint     operator*   (const bigint &oth);
        bigint     operator*   (const ssize_t &oth);
        bigint     operator*   (const size_t &oth);
        bigint     operator*   (const int &oth);
        bigint     operator*   (const double &oth);
        bigint     operator*   (const long double &oth);
        void       operator-=  (const bigint &oth);
        void       operator+=  (const bigint &oth);
        void       operator-=  (const ssize_t &oth);
        void       operator+=  (const ssize_t &oth);
        void       operator-=  (const int &oth);
        void       operator+=  (const int &oth);
        void       operator-=  (const double &oth);
        void       operator+=  (const double &oth);
        void       operator-=  (const long double &oth);
        void       operator+=  (const long double &oth);
        void       operator*=  (const bigint &oth);
        void       operator*=  (const size_t &oth);
        void       operator*=  (const ssize_t &oth);
        void       operator*=  (const double &oth);
        void       operator*=  (const long double &oth);
        void       operator*=  (const int &oth);
        void       operator/=  (const bigint &oth);
        void       operator/=  (const size_t &oth);
        void       operator/=  (const ssize_t &oth);
        void       operator/=  (const double &oth);
        void       operator/=  (const long double &oth);
        void       operator/=  (const int &oth);
        bool       operator>   (const bigint &oth) const;
        bool       operator>  (const size_t &oth) const;
        bool       operator>  (const ssize_t &oth) const;
        bool       operator>  (const double &oth) const;
        bool       operator>  (const int &oth) const;
        bool       operator>  (const std::string &oth) const;
        bool       operator<   (const bigint &oth) const;
        bool       operator<  (const size_t &oth) const;
        bool       operator<  (const ssize_t &oth) const;
        bool       operator<  (const double &oth) const;
        bool       operator<  (const int &oth) const;
        bool       operator<  (const std::string &oth) const;
        bool       operator==  (const bigint &oth) const;
        bool       operator==  (const size_t &oth) const;
        bool       operator==  (const ssize_t &oth) const;
        bool       operator==  (const double &oth) const;
        bool       operator==  (const int &oth) const;
        bool       operator==  (const std::string &oth) const;
        bool       operator!=  (const bigint &oth) const;
        bool       operator!=  (const size_t &oth) const;
        bool       operator!=  (const ssize_t &oth) const;
        bool       operator!=  (const double &oth) const;
        bool       operator!=  (const int &oth) const;
        bool       operator!=  (const std::string &oth) const;
        bool       operator>=  (const bigint &oth) const;
        bool       operator>=  (const size_t &oth) const;
        bool       operator>=  (const ssize_t &oth) const;
        bool       operator>=  (const double &oth) const;
        bool       operator>=  (const int &oth) const;
        bool       operator>=  (const std::string &oth) const;
        bool       operator<=  (const bigint &oth) const;
        bool       operator<=  (const size_t &oth) const;
        bool       operator<=  (const ssize_t &oth) const;
        bool       operator<=  (const double &oth) const;
        bool       operator<=  (const int &oth) const;
        bool       operator<=  (const std::string &oth) const;
        bigint&    operator--  ();
        bigint&    operator++  ();
        bigint     operator!   () const;
        bigint     operator-   () const;
        bigint&    operator=   (const std::string &oth);
        bigint&    operator=   (const bigint &oth);
        bigint&    operator=   (const ssize_t &oth);
        bigint&    operator=   (const size_t &oth);
        bigint&    operator=   (const int &oth);
        bigint&    operator=   (const double &oth);

    private:
        void cleanup();
        mpf_t   x;
    };
}

std::ostream &operator<<(std::ostream &os, const manapi::bigint &m);
std::istream &operator>>(std::istream &is, manapi::bigint &m);

#endif //MANAPIHTTP_MANAPIBIGINT_H
