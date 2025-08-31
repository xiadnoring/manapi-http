/**
 * @file ManapiBigint.hpp
 * @brief Provides utilities to work with large integers and decimals
 *
 * @author Timur Zajnullin
 * @author GMP Team
 */

#pragma once

#include "./ManapiUtils.hpp"

#if MANAPIHTTP_GMP_DEPENDENCY
#define MANAPIHTTP_BIGINT_SUPPORT

#include <string>
#include <memory>

#include "./ManapiInt.hpp"

#define MANAPI_BIGINT_DEFAULT_PRECISION 128

namespace manapi {
    /**
     * Bigint provides utilities to work
     * with large integers and decimals.
     */
    class DLLExportImport bigint {
        struct data_t;

        struct data_t_deleter {
            void operator()(data_t *n) MANAPIHTTP_NOEXCEPT;
        };

    public:
        bigint();

        ~bigint();

        explicit bigint(std::string_view num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION);

        explicit bigint(ssize_t num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION);

        explicit bigint(int num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION);

        explicit bigint(double num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION);

        explicit bigint(long double num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION);

        template<typename T>
        requires(std::is_integral_v<T>)
        explicit bigint (const T &num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION) {
            this->init_(precision);
            this->parse(static_cast<ssize_t>(num));
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        explicit bigint (const T &num, std::size_t precision = MANAPI_BIGINT_DEFAULT_PRECISION) {
            this->init_(precision);
            this->parse(static_cast<long double>(num));
        }

        bigint(bigint &&other) noexcept;

        bigint &operator=(bigint&&other) noexcept;

        bigint (const bigint &other);

        /**
         * Stringify the bigint and return it
         * @return the stringified bigint
         */
        [[nodiscard]] std::string stringify () const;

        /**
         * Integerify the bigint and return it with data loss
         * @return the integerified bigint
         */
        [[nodiscard]] ssize_t integerify () const;

        /**
         * Decimalify the bigint and return it with data loss
         * @return the decimalify bigint
         */
        [[nodiscard]] double decimalify () const;

        /**
         * Get a bigint from the source string
         * @param num the source string
         * @return the error code. ERR_OK if there's no error, but otherwise, it returns ERR_INVALID_ARGUMENT
         */
        int parse (std::string_view num);

        /**
         * Get a bigint from the source integer
         * @param num the source integer
         */
        void parse (ssize_t num);

        /**
         * Get a bigint from the source double
         * @param num the source double
         */
        void parse (double num);

        /**
         * Get a bigint from the source long double
         * @param num the source double
         */
        void parse (long double num);

        /**
         * Set the precision to avoid data loss
         * @param precision the precision to set
         */
        void precision (std::size_t precision);

        /**
         * Get the precision
         * @return the precision
         */
        [[nodiscard]] size_t precision () const;

        bigint operator/ (const bigint &oth) const;

        bigint operator/ (int oth) const;

        bigint operator/ (ssize_t oth) const;

        bigint operator/ (double oth) const;

        bigint operator/ (long double oth) const;

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

        /**
         * Do root operation with bigint
         * @param oth the power
         * @return the result
         */
        [[nodiscard]] bigint root (ssize_t oth) const;

        /**
         * Do sqrt operation with bigint
         * @param oth the power
         * @return the result
         */
        [[nodiscard]] bigint sqrt (ssize_t oth) const;

        bigint operator+ (ssize_t oth) const;

        bigint operator+ (int oth) const;

        bigint operator+ (const bigint &oth) const;

        bigint operator+ (long double oth) const;

        bigint operator+ (double oth) const;

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

        bigint operator- (const bigint &oth) const;

        bigint operator- (int oth) const;

        bigint operator- (ssize_t oth) const;

        bigint operator- (double oth) const;

        bigint operator- (long double oth) const;

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

        bigint operator* (const bigint &oth) const;

        bigint operator* (ssize_t oth) const;

        bigint operator* (int oth) const;

        bigint operator* (double oth) const;

        bigint operator* (long double oth) const;

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

        bigint& operator-= (const bigint &oth);

        bigint& operator-= (ssize_t oth);

        bigint& operator-= (double oth);

        bigint& operator-= (long double oth);

        bigint& operator-= (int oth);

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
        /**
         * initialize the bigint ctx
         * @param precision the precision
         */
        void init_ (std::size_t precision);

        std::unique_ptr<data_t, data_t_deleter> x;
    };
}

#undef MANAPI_BIGINT_DEFAULT_PRECISION

std::ostream &operator<<(std::ostream &os, const manapi::bigint &m);
std::istream &operator>>(std::istream &is, manapi::bigint &m);

#endif