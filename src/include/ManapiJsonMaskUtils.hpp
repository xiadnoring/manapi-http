#pragma once

#include "ManapiJsonMask.hpp"

namespace manapi {
    inline manapi::json format_path (const std::vector<std::string_view> &p) {
        manapi::json a = manapi::json::array();
        a.as_array().reserve(p.size());
        for (const auto &e : p) {
            if (e.data() || !e.size())
                a.push_back(std::string(e));
            else
                a.push_back(std::to_string(e.size()));
        }
        return std::move(a);
    }

    inline manapi::json jm_msg_with_path (std::vector<std::string_view> *p) {
        if (p)
            return {{"path", format_path(*p)}};
        return manapi::json::object();
    }

    template<typename T>
    manapi::json jm_msg_with_path_and_param (std::vector<std::string_view> *p, T &&val) {
        auto obj = manapi::json::object();
        obj.insert({"data", std::forward<T>(val)});
        if (p)
            obj.insert({"path", format_path(*p)});
        return std::move(obj);
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
}
