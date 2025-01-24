#pragma once

#include <stdexcept>
#include <string_view>
#include <array>
#include <typeindex>

#include "ManapiDebug.hpp"

namespace manapi::ext::pq {
    struct oid_pair
    {
        uint32_t single = {};
        uint32_t array  = {};
    };

    using oid_map = std::map<std::type_index, oid_pair>;

    template<typename T>
    struct string_traits {
        static inline T from_string (std::string_view text) { THROW_MANAPIHTTP_EXCEPTION2(ERR_POSTGRE_RESULT, "string_traits not exists"); }
        static inline void to_string (std::string_view text, T const &value) { }
        [[nodiscard]] static inline size_t size (T const &value) { return 0; }
    };

    template<typename T>
    [[nodiscard]] T from_string (std::string_view text) {
        return string_traits<T>::from_string(text);
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
    inline void to_string (std::string_view text, const T &v) {
        return string_traits<T>::to_string(text, v);
    }

    template<typename T>
    inline void to_string (std::string_view text, const T *v) {
        return string_traits<T>::to_string(text, v);
    }
}

#include "./AsyncPostgreValueTypes.hpp"

namespace manapi::ext::pq {
#include <catalog/pg_type_d.h>
    

    inline uint32_t oid_of (const unsigned int &v) {
        return INT4OID;
    }

    inline uint32_t oid_of (const unsigned short &v) {
        return INT2OID;
    }

    inline uint32_t oid_of (const unsigned long long &v) {
        return INT8OID;
    }

    inline uint32_t oid_of (const int &v) {
        return INT4OID;
    }

    inline uint32_t oid_of (const short &v) {
        return INT2OID;
    }

    inline uint32_t oid_of (const long long &v) {
        return INT8OID;
    }

    inline uint32_t oid_of (const std::string &v) {
        return VARCHAROID;
    }

    inline uint32_t oid_of (const char *v) {
        return VARCHAROID;
    }

    inline uint32_t oid_of (const bool &v) {
        return BOOLOID;
    }

    inline uint32_t oid_of (const char &v) {
        return CHAROID;
    }

    inline uint32_t oid_of (const unsigned char &v) {
        return CHAROID;
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
