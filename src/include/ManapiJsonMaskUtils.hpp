#pragma once

#include <valarray>

#include "ManapiDebug.hpp"
#include "ManapiEventStructures.hpp"
#include "json/ManapiJsonMask.hpp"
#include "json/ManapiJsonInternal.hpp"

namespace manapi {
    manapi::json_error::status json_builder_valid_utf_string(std::string_view str);

    std::string json_format_path2 (const std::vector<manapi::json_mask_path_t> &p);

    const constexpr char *json_type_to_str (int type) {
        switch (type) {
            case manapi::json::type_array: return "array";
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            case manapi::json::type_bigint: return "bigint";
#endif
            case manapi::json::type_boolean: return "boolean";
            case manapi::json::type_decimal: return "decimal";
            case manapi::json::type_integer: return "integer";
            case manapi::json::type_null: return "null";
            case manapi::json::type_number: return "number";
            case manapi::json::type_object: return "object";
            case manapi::json::type_pair: return "pair";
            case manapi::json::type_string: return "string";
        }

        return "undefined";
    }

    class json_dump_buf_str : public manapi::json_dump_buffer {
    public:
        json_dump_buf_str (std::string *str);

        void set (std::string *str);

        void push_back(const char *buffer, std::size_t sz) override;

        void push_back(char c) override;
    private:
        std::string *m_str;
    };

    class json_dump_buf_prealloc_sv : public manapi::json_dump_buffer {
    public:
        json_dump_buf_prealloc_sv (manapi::slice *sv);

        void set (manapi::slice *sv);

        void push_back(const char *buffer, std::size_t sz) override;

        void push_back(char c) override;
    private:
        manapi::slice_view m_sv;
    };

    class json_dump_buf_sv : public manapi::json_dump_buffer {
    public:
        json_dump_buf_sv (manapi::slice *sv);

        void set (manapi::slice *sv);

        void push_back(const char *buffer, std::size_t sz) override;

        void push_back(char c) override;
    private:
        manapi::slice *m_sv;
    };

    template<typename T>
    bool json_verify_min_mean (int flags, const manapi::json &n, const T &val) {
        if (flags & JSON_MASK_TYPE_FLAG_S) {
            if ((n > val))
                return false;
        }
        else {
            if ((n >= val))
                return false;
        }
        return true;
    }


    template<typename T>
    bool json_verify_max_mean (int flags, const manapi::json &n, const T &val) {
        if (flags & JSON_MASK_TYPE_FLAG_E) {
            if ((n < val))
                return false;
        }
        else {
            if ((n <= val))
                return false;
        }
        return true;
    }
}
