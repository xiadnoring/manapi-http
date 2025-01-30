#pragma once

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

        explicit bigint(const std::string &num, const unsigned long int &precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(const std::wstring &num, const unsigned long int &precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(const ssize_t &num, const unsigned long int &precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(const int &num, const unsigned long int &precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(const double &num, const unsigned long int &precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(const long double &num, const unsigned long int &precision = MANAPI_BIGINT_DEFAULT_PRECISION);
        explicit bigint(const mpf_t &num, const unsigned long int &precision = MANAPI_BIGINT_DEFAULT_PRECISION);

        template<typename T>
        requires(std::is_integral_v<T>)
        explicit bigint (const T &num, const unsigned long int &precision = MANAPI_BIGINT_DEFAULT_PRECISION) {
            mpf_init2 (this->x, precision);
            this->parse(static_cast<ssize_t>(num));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        explicit bigint (const T &num, const unsigned long int &precision = MANAPI_BIGINT_DEFAULT_PRECISION) {
            mpf_init2 (this->x, precision);
            this->parse(static_cast<long double>(num));
        }

        bigint(bigint &&other) noexcept;
        bigint (const bigint &other);

        [[nodiscard]] std::string stringify () const;
        [[nodiscard]] ssize_t integerify () const;
        [[nodiscard]] double decimalify () const;
 
        void parse (const std::string &num);
        // void parse (const long long int    &num);
        void parse (const ssize_t &num);
        void parse (const double &num);
        void parse (const long double &num);

        void        set_precision (const size_t &precision);
        [[nodiscard]] size_t      get_precision () const;

        bigint     operator/   (const bigint &oth) const;
        bigint     operator/   (const int &oth) const;
        bigint     operator/   (const ssize_t &oth) const;
        bigint     operator/   (const double &oth) const;
        bigint     operator/   (const long double &oth) const;

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

        bigint     operator+   (const ssize_t &oth) const;
        bigint     operator+   (const int &oth) const;
        bigint     operator+   (const bigint &oth) const;
        bigint     operator+   (const long double &oth) const;
        bigint     operator+   (const double &oth) const;

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
        bigint     operator-   (const int &oth) const;
        bigint     operator-   (const ssize_t &oth) const;
        bigint     operator-   (const double &oth) const;
        bigint     operator-   (const long double &oth) const;

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
        bigint     operator*   (const ssize_t &oth) const;
        bigint     operator*   (const int &oth) const;
        bigint     operator*   (const double &oth) const;
        bigint     operator*   (const long double &oth) const;

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
        bigint& operator-=  (const ssize_t &oth);
        bigint& operator-=  (const double &oth);
        bigint& operator-=  (const long double &oth);
        bigint& operator-=  (const int &oth);

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

        bigint& operator+=  (const int &oth);
        bigint& operator+=  (const bigint &oth);
        bigint& operator+=  (const ssize_t &oth);
        bigint& operator+=  (const double &oth);
        bigint& operator+=  (const long double &oth);

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
        bigint& operator*=  (const ssize_t &oth);
        bigint& operator*=  (const double &oth);
        bigint& operator*=  (const long double &oth);
        bigint& operator*=  (const int &oth);


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
        bigint& operator/=  (const ssize_t &oth);
        bigint& operator/=  (const double &oth);
        bigint& operator/=  (const long double &oth);
        bigint& operator/=  (const int &oth);


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
        bool       operator>  (const ssize_t &oth) const;
        bool       operator>  (const double &oth) const;
        bool       operator>  (const int &oth) const;
        bool       operator>  (const std::string &oth) const;
        bool       operator<   (const bigint &oth) const;
        bool       operator<  (const ssize_t &oth) const;
        bool       operator<  (const double &oth) const;
        bool       operator<  (const int &oth) const;
        bool       operator<  (const std::string &oth) const;
        bool       operator==  (const bigint &oth) const;
        bool       operator==  (const ssize_t &oth) const;
        bool       operator==  (const double &oth) const;
        bool       operator==  (const int &oth) const;
        bool       operator==  (const std::string &oth) const;
        bool       operator!=  (const bigint &oth) const;
        bool       operator!=  (const ssize_t &oth) const;
        bool       operator!=  (const double &oth) const;
        bool       operator!=  (const int &oth) const;
        bool       operator!=  (const std::string &oth) const;
        bool       operator>=  (const bigint &oth) const;
        bool       operator>=  (const ssize_t &oth) const;
        bool       operator>=  (const double &oth) const;
        bool       operator>=  (const int &oth) const;
        bool       operator>=  (const std::string &oth) const;
        bool       operator<=  (const bigint &oth) const;
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
        bigint&    operator=   (const int &oth);
        bigint&    operator=   (const double &oth);

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