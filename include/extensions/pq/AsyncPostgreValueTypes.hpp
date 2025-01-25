#pragma once

#include <cassert>
#include <climits>

#include "./AsyncPostgreValue.hpp"

#include <charconv>
#include <cstring>


namespace manapi::ext::pq {
    inline void _reverse_seq (std::string_view ctx) {
        auto s = ctx.size() - 1;
        for (int i = 0; i < ctx.size() / 2; i++) {
            std::swap((char&)(ctx[i]), (char&)(ctx[s-i]));
        }
    }
    template<typename T> struct integral_traits
    {
        static T from_string(std::string_view text_) {
            T res;
            auto s = text_.size();
            if (s != sizeof (T)) {
                if (std::is_unsigned<T> ()) {
                    if (s == sizeof (unsigned char)) {
                        res = static_cast<T>(integral_traits<unsigned char>::from_string (text_));
                    }
                    else if (s == sizeof (unsigned short)) {
                        res = static_cast<T>(integral_traits<unsigned short>::from_string (text_));
                    }
                    else if (s == sizeof (unsigned int)) {
                        res = static_cast<T>(integral_traits<unsigned int>::from_string (text_));
                    }
#ifdef LLONG_MAX
                    else if (s == sizeof (unsigned long long)) {
                        res = static_cast<T>(integral_traits<unsigned long long>::from_string (text_));
                    }
#endif
                    else {
                        THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "invalid size for integral_traits: {}", s);
                    }
                }
                else {
                    if (s == sizeof (char)) {
                        res = static_cast<T>(integral_traits<char>::from_string (text_));
                    }
                    else if (s == sizeof (short)) {
                        res = static_cast<T>(integral_traits<short>::from_string (text_));
                    }
                    else if (s == sizeof (int)) {
                        res = static_cast<T>(integral_traits<int>::from_string (text_));
                    }
#ifdef LLONG_MAX
                    else if (s == sizeof (long long)) {
                        res = static_cast<T>(integral_traits<long long>::from_string (text_));
                    }
#endif
                    else {
                        THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "invalid size for integral_traits: {}", s);
                    }
                }
            }
            else {
                _reverse_seq(text_);
                res = (*((T *)text_.data()));
                _reverse_seq(text_);
            }
            return std::move(res);
        }
        static void to_string (std::string_view text_, T const &v) {
            std::string_view ctx (text_.data(), sizeof(T));
            (*((T *)ctx.data())) = v;
            _reverse_seq(ctx);
        }

        static constexpr size_t size(T const &) noexcept
        {
            return sizeof (T);
        }
    };


    template<typename T> struct float_traits
    {
        static T from_string(std::string_view text_) {
            T res = 0;
            auto s = text_.size();
            if (s != sizeof (T)) {
                if (s==sizeof (double)) {
                    res = static_cast<T>(float_traits<double>::from_string(text_));
                }
                else if (s==sizeof (float)) {
                    res = static_cast<T>(float_traits<float>::from_string(text_));
                }
#ifdef LLONG_MAX
                else if (s==sizeof (long double)) {
                    res = static_cast<T>(float_traits<long double>::from_string(text_));
                }
#endif
                else {
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "invalid size for float_traits: {}", s);
                }
            }
            else {
                _reverse_seq(text_);
                res = (*((T *)text_.data()));
                _reverse_seq(text_);
            }
            return res;
        }
        static void to_string (std::string_view text_, T const &v) {
            std::string_view ctx (text_.data(), sizeof(T));
            (*((T *)ctx.data())) = v;
            _reverse_seq(ctx);
        }

        static constexpr size_t size(T const &) noexcept
        {
            return sizeof (T);
        }
    };

    /* bool */
    template<> struct string_traits<bool> {

        static constexpr bool from_string (std::string_view text_) {
            return !text_.empty() && text_[0]=='\001';
        }
        static constexpr void to_string (std::string_view text_, bool const &value) {
            assert(text_.size() >= size(value));
            if (value) { memset((void*)text_.data(), '\001', 1); }
            else { memset((void*)text_.data(), '\000', 1); }
        }
        [[nodiscard]] static size_t size (bool const &value) {
            return 1;
        }
    };

    /* string */
    template<> struct string_traits <std::string> {
        static constexpr std::string from_string (std::string_view text_) {
            return std::string{text_};
        }
        static constexpr void to_string (std::string_view text_, std::string const &value) {
            assert(text_.size() == value.size());
            memcpy((void*)text_.data(), value.data(), value.size());
        }
        [[nodiscard]] static size_t size (std::string const &value) {
            return value.size();
        }
    };

    /* string */
    template<> struct string_traits <const char *> {
        static constexpr std::string from_string (std::string_view text_) {
            return std::string{text_};
        }
        static constexpr void to_string (std::string_view text_, const char *value) {
            memcpy((void*)text_.data(), value, strlen(value));
        }
        [[nodiscard]] static size_t size (const char *value) {
            return strlen(value);
        }
    };


    template<> [[nodiscard]] inline unsigned int from_string (std::string_view text_) {
        return integral_traits<unsigned int>::from_string(text_);
    }

    template<> [[nodiscard]] inline short from_string (std::string_view text_) {
        return integral_traits<short>::from_string(text_);
    }

    template<> [[nodiscard]] inline std::string from_string (std::string_view text_) {
        return string_traits <std::string>::from_string(text_);
    }

    template<> [[nodiscard]] inline unsigned short from_string (std::string_view text_) {
        return integral_traits<unsigned short>::from_string(text_);
    }

    template<> [[nodiscard]] inline char from_string (std::string_view text_) {
        return integral_traits<char>::from_string(text_);
    }

    template<> [[nodiscard]] inline unsigned char from_string (std::string_view text_) {
        return integral_traits<unsigned char>::from_string(text_);
    }

    template<> [[nodiscard]] inline int from_string (std::string_view text_) {
        return integral_traits<int>::from_string(text_);
    }


    template<> [[nodiscard]] inline size_t from_string (std::string_view text_) {
        return integral_traits<size_t>::from_string(text_);
    }

    template<> [[nodiscard]] inline ssize_t from_string (std::string_view text_) {
        return integral_traits<ssize_t>::from_string(text_);
    }

    template<> [[nodiscard]] inline double from_string (std::string_view text_) {
        return float_traits <double>::from_string(text_);
    }

    template<> [[nodiscard]] inline float from_string (std::string_view text_) {
        return float_traits <float>::from_string(text_);
    }

    template<> [[nodiscard]] inline size_t size_of (const unsigned int &v) {
        return integral_traits<unsigned int>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const int &v) {
        return integral_traits<int>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const short &v) {
        return integral_traits<short>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const unsigned short &v) {
        return integral_traits<unsigned short>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const unsigned char &v) {
        return integral_traits<unsigned char>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const char &v) {
        return integral_traits<char>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const ssize_t &v) {
        return integral_traits<ssize_t>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const size_t &v) {
        return integral_traits<size_t>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const std::string &v) {
        return string_traits<std::string>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const pq::blob &v) {
        return string_traits<pq::blob>::size(v);
    }
    template<> [[nodiscard]] inline size_t size_of (const pq::text &v) {
        return string_traits<pq::text>::size(v);
    }


    template<> [[nodiscard]] inline size_t size_of (const bool &v) {
        return string_traits<bool>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const float &v) {
        return float_traits<float>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const double &v) {
        return float_traits<double>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const char *v) {
        return string_traits<const char *>::size(v);
    }

    template<> inline void to_string (std::string_view text_, const bool &v) {
        return string_traits<bool>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const std::string &v) {
        return string_traits<std::string>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const pq::blob &v) {
        return string_traits<pq::blob>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const pq::text &v) {
        return string_traits<pq::text>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const char *v) {
        return string_traits<const char *>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const int &v) {
        return integral_traits<int>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const unsigned int &v) {
        return integral_traits<unsigned int>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const char &v) {
        return integral_traits<char>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const unsigned char &v) {
        return integral_traits<unsigned char>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const short &v) {
        return integral_traits<short>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const unsigned short &v) {
        return integral_traits<unsigned short>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const size_t &v) {
        return integral_traits<size_t>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const ssize_t &v) {
        return integral_traits<ssize_t>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const double &v) {
        return float_traits <double>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const float &v) {
        return float_traits <float>::to_string(text_, v);
    }

#ifdef LLONG_MAX

    template<> [[nodiscard]] inline long double from_string (std::string_view text_) {
        return float_traits <long double>::from_string(text_);
    }

    template<> [[nodiscard]] inline size_t size_of (const long double &v) {
        return float_traits<long double>::size(v);
    }

    template<> inline void to_string (std::string_view text_, const long double &v) {
        return float_traits <long double>::to_string(text_, v);
    }

    template<> [[nodiscard]] inline long long from_string (std::string_view text_) {
        return integral_traits<long long>::from_string(text_);
    }

    template<> [[nodiscard]] inline unsigned long long from_string (std::string_view text_) {
        return integral_traits<unsigned long long>::from_string(text_);
    }

    template<> [[nodiscard]] inline size_t size_of (const long long &v) {
        return integral_traits<long long>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const unsigned long long &v) {
        return integral_traits<unsigned long long>::size(v);
    }

    template<> inline void to_string (std::string_view text_, const long long &v) {
        return integral_traits<long long>::to_string(text_, v);
    }

    template<> inline void to_string (std::string_view text_, const unsigned long long &v) {
        return integral_traits<unsigned long long>::to_string(text_, v);
    }

#endif
}