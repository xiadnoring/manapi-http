#pragma once

#include <cassert>
#include <climits>
#include <charconv>
#include <cstring>

#include "./PostgreValue.hpp"
#include "./../../ManapiUtils.hpp"
#include "./../../crypto/ManapiAES.hpp"


namespace manapi::ext::pq {
    template<typename T> struct integral_traits
    {
        using U = std::make_unsigned_t<T>;

        static T from_string(std::string_view text) {

            T res = 0;
            auto const max_size = std::min<std::size_t> ( static_cast<std::size_t> (sizeof (T)), text.size() );
            for (std::size_t i = 0; i < max_size; i++) {
                res = static_cast<T>(static_cast<U>(res << 8) | static_cast<uint8_t>(text[i]));
            }

            return (res);
        }
        static void to_string (std::string_view text, T const &v) {
            T z = v;
            char *c = const_cast<char *>(text.data()) + sizeof (T);
            for (std::size_t i = 0; i < static_cast<std::size_t>(sizeof (T)); i++) {
                *(--c) = static_cast<char>(static_cast<U>(z) & 0xFF);
                z = static_cast<T> (static_cast<U> (z) >> 8);
            }
        }

        static constexpr size_t size(T const &) MANAPIHTTP_NOEXCEPT
        {
            return sizeof (T);
        }
    };


    template<typename T> struct float_traits
    {
        static T from_string(std::string_view text) {
            T res = 0;
            auto s = text.size();
            if (s != sizeof (T)) {
                if (s==sizeof (double)) {
                    res = static_cast<T>(float_traits<double>::from_string(text));
                }
                else if (s==sizeof (float)) {
                    res = static_cast<T>(float_traits<float>::from_string(text));
                }
                else if (s==sizeof (long double)) {
                    res = static_cast<T>(float_traits<long double>::from_string(text));
                }
                else {
                    throw std::runtime_error("invalid size for float_traits");
                }
            }
            else {
                char *buffer = reinterpret_cast<char *> (&res);
                std::memcpy (buffer, text.data(), sizeof (T));
                if constexpr (std::endian::native == std::endian::little) {
                    std::reverse (buffer, buffer + sizeof (T));
                }
            }
            return res;
        }
        static void to_string (std::string_view text, T const &v) {
            std::memcpy (const_cast<char *>(text.data()), &v, sizeof (v));
            if constexpr ((std::endian::native == std::endian::little)) {
                std::reverse (const_cast<char *>(text.data()), const_cast<char *>(text.data() + text.size()));
            }
        }

        static constexpr size_t size(T const &) MANAPIHTTP_NOEXCEPT
        {
            return sizeof (T);
        }
    };

    /* bool */
    template<> struct string_traits<bool> {

        static constexpr bool from_string (std::string_view text) {
            return !text.empty() && text[0]=='\001';
        }
        static constexpr void to_string (std::string_view text, bool const &value) {
            assert(text.size() >= size(value));
            if (value) { memset((void*)text.data(), '\001', 1); }
            else { memset((void*)text.data(), '\000', 1); }
        }
        MANAPIHTTP_NODISCARD static size_t size (bool const &value) {
            return 1;
        }
    };

    /* string */
    template<> struct string_traits <std::string> {
        static constexpr std::string from_string (std::string_view text) {
            return std::string{text};
        }
        static constexpr void to_string (std::string_view text, std::string const &value) {
            assert(text.size() >= value.size());
            memcpy((void*)text.data(), value.data(), value.size());
        }
        MANAPIHTTP_NODISCARD static size_t size (std::string const &value) {
            return value.size();
        }
    };

    /* string */
    template<> struct string_traits <const char *> {
        static constexpr const char *from_string (std::string_view text) {
            return text.data();
        }
        static constexpr void to_string (std::string_view text, const char *value) {
            memcpy((void*)text.data(), value, strlen(value));
        }
        MANAPIHTTP_NODISCARD static size_t size (const char *value) {
            return ::strlen(value);
        }
    };

    template<> MANAPIHTTP_NODISCARD inline std::string_view from_string (std::string_view text) {
        return string_traits<std::string_view>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline unsigned int from_string (std::string_view text) {
        return integral_traits<unsigned int>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline short from_string (std::string_view text) {
        return integral_traits<short>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline std::string from_string (std::string_view text) {
        return string_traits <std::string>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline unsigned short from_string (std::string_view text) {
        return integral_traits<unsigned short>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline char from_string (std::string_view text) {
        return integral_traits<char>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline unsigned char from_string (std::string_view text) {
        return integral_traits<unsigned char>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline int from_string (std::string_view text) {
        return integral_traits<int>::from_string(text);
    }


    template<> MANAPIHTTP_NODISCARD inline size_t from_string (std::string_view text) {
        return integral_traits<size_t>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline ssize_t from_string (std::string_view text) {
        return integral_traits<ssize_t>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline double from_string (std::string_view text) {
        return float_traits <double>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline float from_string (std::string_view text) {
        return float_traits <float>::from_string(text);
    }

    template<> inline pq::uuid from_string (std::string_view text) {
        auto m_uuid = pq::uuid{manapi::crypto::strdec2strhex(text).unwrap()};
        return std::move(m_uuid);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const unsigned int &v) {
        return integral_traits<unsigned int>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const int &v) {
        return integral_traits<int>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const short &v) {
        return integral_traits<short>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const unsigned short &v) {
        return integral_traits<unsigned short>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const unsigned char &v) {
        return integral_traits<unsigned char>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const char &v) {
        return integral_traits<char>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const ssize_t &v) {
        return integral_traits<ssize_t>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const size_t &v) {
        return integral_traits<size_t>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const std::string &v) {
        return string_traits<std::string>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const std::string_view &v) {
        return string_traits<std::string_view>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const pq::blob &v) {
        return string_traits<pq::blob>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const pq::text &v) {
        return string_traits<pq::text>::size(v);
    }

    template<> [[nodiscard]] inline size_t size_of (const pq::uuid &v) {
        return 16;
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const bool &v) {
        return string_traits<bool>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const float &v) {
        return float_traits<float>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const double &v) {
        return float_traits<double>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const char *v) {
        return string_traits<const char *>::size(v);
    }

    template<> inline void to_string (std::string_view text, const bool &v) {
        return string_traits<bool>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const std::string &v) {
        return string_traits<std::string>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const pq::blob &v) {
        return string_traits<pq::blob>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const pq::text &v) {
        return string_traits<pq::text>::to_string(text, v);
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

    template<> inline void to_string (std::string_view text, const size_t &v) {
        return integral_traits<size_t>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const ssize_t &v) {
        return integral_traits<ssize_t>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const double &v) {
        return float_traits <double>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const float &v) {
        return float_traits <float>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const std::string_view &v) {
        return string_traits <std::string_view>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const pq::uuid &v) {
        auto uuid_ = manapi::crypto::strhex2strdec(v).unwrap();
        assert(text.size() >= uuid_.size());
        memcpy((void *)text.data(), uuid_.data(), uuid_.size());
    }

#ifdef LLONG_MAX

    template<> MANAPIHTTP_NODISCARD inline long double from_string (std::string_view text) {
        return float_traits <long double>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const long double &v) {
        return float_traits<long double>::size(v);
    }

    template<> inline void to_string (std::string_view text, const long double &v) {
        return float_traits <long double>::to_string(text, v);
    }

    template<> MANAPIHTTP_NODISCARD inline long long from_string (std::string_view text) {
        return integral_traits<long long>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline unsigned long long from_string (std::string_view text) {
        return integral_traits<unsigned long long>::from_string(text);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const long long &v) {
        return integral_traits<long long>::size(v);
    }

    template<> MANAPIHTTP_NODISCARD inline size_t size_of (const unsigned long long &v) {
        return integral_traits<unsigned long long>::size(v);
    }

    template<> inline void to_string (std::string_view text, const long long &v) {
        return integral_traits<long long>::to_string(text, v);
    }

    template<> inline void to_string (std::string_view text, const unsigned long long &v) {
        return integral_traits<unsigned long long>::to_string(text, v);
    }
#endif
}
