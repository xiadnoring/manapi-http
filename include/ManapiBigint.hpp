#pragma once

#include "ManapiUtils.hpp"

#if MANAPIHTTP_GMP_DEPENDENCY
#define MANAPIHTTP_BIGINT_SUPPORT

#include <string>
#include <vector>
#include <iostream>
#include "ManapiInt.hpp"
#include "gmp.h"

#define MANAPI_BIGINT_DEFAULT_PRECISION 128

namespace manapi {
    class bigint {
    public:
        bigint();
        ~bigint();

        explicit bigint(std::string_view num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(const std::wstring &num, unsigned long int precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(ssize_t num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(int num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(double num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(long double num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(const mpf_t &num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION);

        template<typename T>
        requires(std::is_integral_v<T>)
        explicit bigint (const T &num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION) {
            mpf_init2 (this->x, precision);
            this->parse(static_cast<ssize_t>(num));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        explicit bigint (const T &num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION) {
            mpf_init2 (this->x, precision);
            this->parse(static_cast<long double>(num));
        }

        bigint(bigint &&other) noexcept;
        bigint (const bigint &other);

        [[nodiscard]] std::string stringify () const;
        [[nodiscard]] ssize_t integerify () const;
        [[nodiscard]] double decimalify () const;

        void parse (std::string_view num);
        // void parse (const long long int    &num);
        void parse (ssize_t num);
        void parse (double num);
        void parse (long double num);

        void        set_precision (std::size_t precision);
        [[nodiscard]] size_t      get_precision () const;

        bigint     operator/   (const bigint &oth) const;
        bigint     operator/   (int oth) const;
        bigint     operator/   (ssize_t oth) const;
        bigint     operator/   (double oth) const;
        bigint     operator/   (long double oth) const;

        template<typename T>
        requires(std::is_integral_v<T>)
        bigint operator/ (const T &v) const {
            return this->operator/(static_cast<ssize_t> (v));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bigint operator/ (const T &v) const {
            return this->operator/(static_cast<long double> (v));
        }

        bigint     root   (ssize_t oth) const;

        bigint     sqrt   (ssize_t oth) const;

        bigint     operator+   (ssize_t oth) const;
        bigint     operator+   (int oth) const;
        bigint     operator+   (const bigint &oth) const;
        bigint     operator+   (long double oth) const;
        bigint     operator+   (double oth) const;

        template<typename T>
        requires(std::is_integral_v<T>)
        bigint operator+ (const T &v) const {
            return this->operator+(static_cast<ssize_t> (v));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bigint operator+ (const T &v) const {
            return this->operator+(static_cast<long double> (v));
        }

        bigint     operator-   (const bigint &oth) const;
        bigint     operator-   (int oth) const;
        bigint     operator-   (ssize_t oth) const;
        bigint     operator-   (double oth) const;
        bigint     operator-   (long double oth) const;

        template<typename T>
        requires(std::is_integral_v<T>)
        bigint operator- (const T &v) const {
            return this->operator-(static_cast<ssize_t> (v));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bigint operator-(const T &v) const {
            return this->operator-(static_cast<long double> (v));
        }

        bigint     operator*   (const bigint &oth) const;
        bigint     operator*   (ssize_t oth) const;
        bigint     operator*   (int oth) const;
        bigint     operator*   (double oth) const;
        bigint     operator*   (long double oth) const;

        template<typename T>
        requires(std::is_integral_v<T>)
        bigint operator* (const T &v) const {
            return this->operator*(static_cast<ssize_t> (v));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bigint operator* (const T &v) const {
            return this->operator*(static_cast<long double> (v));
        }

        bigint& operator-=  (const bigint &oth);
        bigint& operator-=  (ssize_t oth);
        bigint& operator-=  (double oth);
        bigint& operator-=  (long double oth);
        bigint& operator-=  (int oth);

        template<typename T>
        requires(std::is_integral_v<T>)
        bigint &operator-= (const T &v) {
            return this->operator-=(static_cast<ssize_t> (v));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bigint &operator-= (const T &v) {
            return this->operator-=(static_cast<long double> (v));
        }

        bigint& operator+=  (int oth);
        bigint& operator+=  (const bigint &oth);
        bigint& operator+=  (ssize_t oth);
        bigint& operator+=  (double oth);
        bigint& operator+=  (long double oth);

        template<typename T>
        requires(std::is_integral_v<T>)
        bigint &operator+= (const T &v) {
            return this->operator+=(static_cast<ssize_t> (v));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bigint &operator+= (const T &v) {
            return this->operator+=(static_cast<long double> (v));
        }

        bigint& operator*=  (const bigint &oth);
        bigint& operator*=  (ssize_t oth);
        bigint& operator*=  (double oth);
        bigint& operator*=  (long double oth);
        bigint& operator*=  (int oth);


        template<typename T>
        requires(std::is_integral_v<T>)
        bigint &operator*= (const T &v) {
            return this->operator*=(static_cast<ssize_t> (v));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bigint &operator*= (const T &v) {
            return this->operator*=(static_cast<long double> (v));
        }

        bigint& operator/=  (const bigint &oth);
        bigint& operator/=  (ssize_t oth);
        bigint& operator/=  (double oth);
        bigint& operator/=  (long double oth);
        bigint& operator/=  (int oth);


        template<typename T>
        requires(std::is_integral_v<T>)
        bigint &operator/= (const T &v) {
            return this->operator/=(static_cast<ssize_t> (v));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bigint &operator/= (const T &v) {
            return this->operator/=(static_cast<long double> (v));
        }

        bool       operator>   (const bigint &oth) const;
        bool       operator>  (ssize_t oth) const;
        bool       operator>  (double oth) const;
        bool       operator>  (int oth) const;
        bool       operator>  (std::string_view oth) const;
        bool       operator<   (const bigint &oth) const;
        bool       operator<  (ssize_t oth) const;
        bool       operator<  (double oth) const;
        bool       operator<  (int oth) const;
        bool       operator<  (std::string_view oth) const;
        bool       operator==  (const bigint &oth) const;
        bool       operator==  (ssize_t oth) const;
        bool       operator==  (double oth) const;
        bool       operator==  (int oth) const;
        bool       operator==  (std::string_view oth) const;
        bool       operator!=  (const bigint &oth) const;
        bool       operator!=  (ssize_t oth) const;
        bool       operator!=  (double oth) const;
        bool       operator!=  (int oth) const;
        bool       operator!=  (std::string_view oth) const;
        bool       operator>=  (const bigint &oth) const;
        bool       operator>=  (ssize_t oth) const;
        bool       operator>=  (double oth) const;
        bool       operator>=  (int oth) const;
        bool       operator>=  (std::string_view oth) const;
        bool       operator<=  (const bigint &oth) const;
        bool       operator<=  (ssize_t oth) const;
        bool       operator<=  (double oth) const;
        bool       operator<=  (int oth) const;
        bool       operator<=  (std::string_view oth) const;
        bigint&    operator--  ();
        bigint&    operator++  ();
        bigint     operator!   () const;
        bigint     operator-   () const;
        bigint&    operator=   (std::string_view oth);
        bigint&    operator=   (const bigint &oth);
        bigint&    operator=   (ssize_t oth);
        bigint&    operator=   (int oth);
        bigint&    operator=   (double oth);

        template<typename T>
        requires(std::is_integral_v<T>)
        bigint &operator= (const T &v) {
            this->operator=(static_cast<ssize_t> (v));
            return *this;
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        bigint &operator= (const T &v) {
            this->operator=(static_cast<double> (v));
            return *this;
        }

    private:
        void cleanup();
        mpf_t x;
    };
}

#undef MANAPI_BIGINT_DEFAULT_PRECISION

std::ostream &operator<<(std::ostream &os, const manapi::bigint &m);
std::istream &operator>>(std::istream &is, manapi::bigint &m);

#endif