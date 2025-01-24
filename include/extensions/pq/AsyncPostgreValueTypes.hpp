#pragma once

#include <assert.h>

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
        static T from_string(std::string_view text) {
            std::string_view ctx (text.data(), sizeof(T));
            _reverse_seq(ctx);
            auto res = (*((T *)ctx.data()));
            _reverse_seq(ctx);
            return res;
        }
        static void to_string (std::string_view text, T const &v) {
            std::string_view ctx (text.data(), sizeof(T));
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
        static T from_string(std::string_view text) {
            T result;
            auto [ptr, err] = std::from_chars(text.data(), text.data() + text.size(), result);
            if (err == std::errc{} && ptr == text.data() + text.size()) {
                return result;
            }

            THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_RESULT, "invalid intergral value");
        }
        static void to_string (std::string_view text, T const &v) {
            auto a = std::to_string(v);
            assert(text.size() >= a.size());
            memcpy((void *)text.data(), a.data(), a.size());
        }

        static constexpr size_t size(T const &) noexcept
        {
            return sizeof (T);
        }
    };

    /* bool */
    template<> struct string_traits<bool> {
        static constexpr char t[] = {"true"};
        static constexpr char f[] = {"false"};

        static constexpr bool from_string (std::string_view text) {
            return text[0] == 't';
        }
        static constexpr void to_string (std::string_view text, bool const &value) {
            assert(text.size() >= size(value));
            if (value) { memcpy((void*)text.data(), t, sizeof (t) - 1); }
            else { memcpy((void*)text.data(), f, sizeof (f) - 1); }
        }
        [[nodiscard]] static size_t size (bool const &value) {
            return value ? sizeof(t) : sizeof(f);
        }
    };

    /* string */
    template<> struct string_traits <std::string> {
        static constexpr std::string from_string (std::string_view text) {
            return std::string{text};
        }
        static constexpr void to_string (std::string_view text, std::string const &value) {
            assert(text.size() == value.size());
            memcpy((void*)text.data(), value.data(), value.size());
        }
        [[nodiscard]] static size_t size (std::string const &value) {
            return value.size();
        }
    };

    /* string */
    template<> struct string_traits <const char *> {
        static constexpr std::string from_string (std::string_view text) {
            return std::string{text};
        }
        static constexpr void to_string (std::string_view text, const char *value) {
            memcpy((void*)text.data(), value, strlen(value));
        }
        [[nodiscard]] static size_t size (const char *value) {
            return strlen(value);
        }
    };


    template<> [[nodiscard]] inline unsigned int from_string (std::string_view text) {
        return integral_traits<unsigned int>::from_string(text);
    }

    template<> [[nodiscard]] inline short from_string (std::string_view text) {
        return integral_traits<short>::from_string(text);
    }

    template<> [[nodiscard]] inline unsigned short from_string (std::string_view text) {
        return integral_traits<unsigned short>::from_string(text);
    }

    template<> [[nodiscard]] inline char from_string (std::string_view text) {
        return integral_traits<char>::from_string(text);
    }

    template<> [[nodiscard]] inline unsigned char from_string (std::string_view text) {
        return integral_traits<unsigned char>::from_string(text);
    }

    template<> [[nodiscard]] inline int from_string (std::string_view text) {
        return integral_traits<int>::from_string(text);
    }

    template<> [[nodiscard]] inline long long from_string (std::string_view text) {
        return integral_traits<long long>::from_string(text);
    }

    template<> [[nodiscard]] inline size_t from_string (std::string_view text) {
        return integral_traits<size_t>::from_string(text);
    }

    template<> [[nodiscard]] inline ssize_t from_string (std::string_view text) {
        return integral_traits<ssize_t>::from_string(text);
    }

    template<> [[nodiscard]] inline unsigned long long from_string (std::string_view text) {
        return integral_traits<unsigned long long>::from_string(text);
    }

    template<> [[nodiscard]] inline float from_string (std::string_view text) {
        return float_traits <float>::from_string(text);
    }

    template<> [[nodiscard]] inline double from_string (std::string_view text) {
        return float_traits <double>::from_string(text);
    }

    template<> [[nodiscard]] inline double long from_string (std::string_view text) {
        return float_traits <double long>::from_string(text);
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

    template<> [[nodiscard]] inline size_t size_of (const long long &v) {
        return integral_traits<long long>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const unsigned long long &v) {
        return integral_traits<unsigned long long>::size(v);
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

    template<> [[nodiscard]] inline size_t size_of (const bool &v) {
        return string_traits<bool>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const float &v) {
        return float_traits<float>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const double &v) {
        return float_traits<double>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const long double &v) {
        return float_traits<long double>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const char *v) {
        return string_traits<const char *>::size(v);
    }

    template<> inline void to_string (std::string_view text, const bool &v) {
        return string_traits<bool>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const std::string &v) {
        return string_traits<std::string>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const char *v) {
        return string_traits<const char *>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const int &v) {
        return integral_traits<int>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const unsigned int &v) {
        return integral_traits<unsigned int>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const char &v) {
        return integral_traits<char>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const unsigned char &v) {
        return integral_traits<unsigned char>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const short &v) {
        return integral_traits<short>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const unsigned short &v) {
        return integral_traits<unsigned short>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const long long &v) {
        return integral_traits<long long>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const unsigned long long &v) {
        return integral_traits<unsigned long long>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const size_t &v) {
        return integral_traits<size_t>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const ssize_t &v) {
        return integral_traits<ssize_t>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const long double &v) {
        return float_traits <long double>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const double &v) {
        return float_traits <double>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const float &v) {
        return float_traits <float>::to_string(text, v);
    }

}