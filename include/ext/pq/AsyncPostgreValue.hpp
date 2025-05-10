#pragma once

#include <stdexcept>
#include <string_view>
#include <array>
#include <typeindex>
#include <memory.h>

#include "../../ManapiUtils.hpp"
#include "../../ManapiDebug.hpp"

namespace manapi::ext::pq {
    class text : public std::string_view {
    public:
        text (const char *a, const size_t &s) : std::string_view(a, s) {}
        text (const char *a) : std::string_view(a) {}
        text (const std::string &a) : std::string_view(a) {}
        text (const std::string_view &a) : std::string_view(a) {}
        text (std::string::iterator first, std::string::iterator last): std::string_view(first, last) {}
        text (std::string::const_iterator first, std::string::const_iterator last): std::string_view(first, last) {}
    };

    class blob : public std::string_view {
    public:
        blob (const char *a, const size_t &s) : std::string_view(a, s) {}
        blob (const char *a) : std::string_view(a) {}
        blob (const std::string &a) : std::string_view(a) {}
        blob (const std::string_view &a) : std::string_view(a) {}
        blob (std::string::iterator first, std::string::iterator last): std::string_view(first, last) {}
        blob (std::string::const_iterator first, std::string::const_iterator last): std::string_view(first, last) {}
    };

    template<typename T>
    struct string_traits {
        static inline T from_string (std::string_view text_) { return std::string{text_}; }
        static inline void to_string (std::string_view text_, T const &value) {
            assert(text_.size() >= value.size()); memcpy((void*)text_.data(), value.data(), value.size());
        }
        [[nodiscard]] static inline size_t size (T const &value) { return value.size(); }
    };

    template<typename T>
    [[nodiscard]] T from_string (std::string_view text_) {
        return string_traits<T>::from_string(text_);
    }

    template<typename T>
    [[nodiscard]] size_t size_of (const T *v) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "template was used");
    }

    template<typename T>
    [[nodiscard]] size_t size_of (const T &v) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_ERROR, "template was used");
    }

    template<typename T>
    inline void to_string (std::string_view text_, const T &v) {
        return string_traits<T>::to_string(text_, v);
    }

    template<typename T>
    inline void to_string (std::string_view text_, const T *v) {
        return string_traits<T>::to_string(text_, v);
    }
}

#include "./AsyncPostgreValueTypes.hpp"

namespace manapi::ext::pq {
#include <catalog/pg_type_d.h>
    template<typename T>
    inline uint32_t oid_of (const T &v) {
        fprintf(stderr, "Unresolved function: oid_of(...)");
        exit(1);
    }


    template<typename T>
    inline uint32_t oid_of (const T *v) {
        fprintf(stderr, "Unresolved function: oid_of(...)");
        exit(1);
    }

    template<> inline uint32_t oid_of (const unsigned int &v) {
        return INT4OID;
    }

    template<> inline uint32_t oid_of (const unsigned short &v) {
        return INT2OID;
    }

    template<> inline uint32_t oid_of (const unsigned long long &v) {
        return INT8OID;
    }

    template<> inline uint32_t oid_of (const size_t &v) {
        return INT8OID;
    }

    template<> inline uint32_t oid_of (const int &v) {
        return INT4OID;
    }

    template<> inline uint32_t oid_of (const std::string_view &v) {
        return VARCHAROID;
    }

    template<> inline uint32_t oid_of (const pq::text &v) {
        return TEXTOID;
    }

    template<> inline uint32_t oid_of (const pq::blob &v) {
        return BYTEAOID;
    }

    template<> inline uint32_t oid_of (const short &v) {
        return INT2OID;
    }

    template<> inline uint32_t oid_of (const long long &v) {
        return INT8OID;
    }

    template<> inline uint32_t oid_of (const ssize_t &v) {
        return INT8OID;
    }

    template<> inline uint32_t oid_of (const std::string &v) {
        return VARCHAROID;
    }

    template<> inline uint32_t oid_of (const char *v) {
        return VARCHAROID;
    }

    template<> inline uint32_t oid_of (const bool &v) {
        return BOOLOID;
    }

    template<> inline uint32_t oid_of (const char &v) {
        return CHAROID;
    }

    template<> inline uint32_t oid_of (const unsigned char &v) {
        return CHAROID;
    }

    template<> inline uint32_t oid_of (const float &v) {
        return FLOAT4OID;
    }

    template<> inline uint32_t oid_of (const double &v) {
        return FLOAT8OID;
    }

    template<typename T>
    [[nodiscard]] inline const char *serialize_param (const T v, int len, std::string_view &buffer) {
        pq::to_string<T>(buffer, v);
        const char *start = buffer.data();
        buffer = buffer.substr(len);
        return start;
    }

    template<typename ...Args>
    auto serialize (std::string &buffer, const std::tuple<Args...>& params) {
        struct result_type {
            std::array<uint32_t, sizeof...(Args)> types;
            std::array<const char *, sizeof...(Args)> values;
            std::array<int, sizeof...(Args)> lengths;
            std::array<int, sizeof...(Args)> formats;
        };

        return std::apply(
            [&] (const auto &...args) -> result_type {
                std::array<int, sizeof...(args)> lengths = { static_cast<int>(size_of(args))... };
                size_t size = 0;

                for (auto &len: lengths)
                {
                    size += len;
                }

                buffer.clear();
                buffer.resize(size);

                std::string_view window {buffer.begin(), buffer.end()};
                int index = 0;

                return result_type {
                    .types = { (oid_of(args))... },
                    .values = { (serialize_param(args, lengths[index++], window))... },
                    .lengths = std::move(lengths),
                    .formats = { ((void)args, true)... },
                };
            }, params);
    }
}
