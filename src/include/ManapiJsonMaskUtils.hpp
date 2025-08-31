#pragma once

#include <valarray>

#include "ManapiDebug.hpp"
#include "ManapiEventStructures.hpp"
#include "json/ManapiJsonMask.hpp"

namespace manapi {
    inline std::string json_format_path (const std::vector<ev::buff_t> *p) {
        std::string res;
        if (p && !p->empty()) {
            std::size_t size = 0;
            for (auto &c : *p) {
                if (c.base)
                    size += c.len;
                else
                    size += static_cast<std::size_t>(std::log10(c.len));
            }
            size += p->size() - 1;
            res.reserve(size);
            for (const auto &c : *p) {
                if (c.base) {
                    res.append(c.base, c.len);
                }
                else {
                    auto n = static_cast<std::size_t>(ceil(std::log10(c.len)));
                    res.resize(res.size() + n + 1);
                    auto err = std::snprintf(res.data() + res.size() - n - 1, n + 1, "%zu", static_cast<std::size_t>(c.len));
                    res.resize(res.size() - 1);
                    if (err < 0)
                        manapi_log_error("bug:snprintf() return < 0 on format path");
                }
                res += '/';
            }
        }

        return std::move(res);
    }

    const constexpr char *json_type_to_str (manapi::json::types type) {
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

        return "none";
    }

    template<typename T>
    bool json_verify_min_mean (const manapi::json &m, T val) {
        if (m[1].as_bool()) {
            if ((m[0] > val))
                return false;
        }
        else {
            if ((m[0] >= val))
                return false;
        }
        return true;
    }


    template<typename T>
    bool json_verify_max_mean (const manapi::json &m, T val) {
        if (m[1].as_bool()) {
            if ((m[0] < val))
                return false;
        }
        else {
            if ((m[0] <= val))
                return false;
        }
        return true;
    }
}
