#include "ManapiJsonMask.hpp"

#include <utility>

#include "async/ManapiAsyncContext.hpp"
#include "ManapiDebug.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJsonBuilder.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "include/ManapiJsonMaskUtils.hpp"

#define MANAPIHTTP_JSON_ANY (-1)
#define MANAPIHTTP_JSON_NONE (-2)

#define MANAPIHTTP_MASK_COMPARE_NONE (-1)
#define MANAPIHTTP_MASK_COMPARE_EQUAL 0
#define MANAPIHTTP_MASK_COMPARE_GREATER 1
#define MANAPIHTTP_MASK_COMPARE_LESS 2
#define MANAPIHTTP_MASK_COMPARE_EQUAL_OR_GREATER 3
#define MANAPIHTTP_MASK_COMPARE_EQUAL_OR_LESS 4

#define THROW_MANAPIHTTP_JSON_ERROR(errnum, msg, ...) throw manapi::json_parse_exception(errnum, std::format(msg, __VA_ARGS__));
#define THROW_MANAPIHTTP_JSON_ERROR2(errnum, msg) throw manapi::json_parse_exception(errnum, std::format(msg));

manapi::json_mask::json_mask(const std::initializer_list<json> &data)
{
    this->information = data;
    if (this->information.is_null()) {
        this->enabled = false;
        return;
    }
    initial_resolve_information (this->information);
    this->enabled = true;
}

manapi::json_mask::json_mask(json data) {
    this->information = std::move(data);
    initial_resolve_information(this->information);
    this->enabled = true;
}

// manapi::json_mask &manapi::json_mask::operator=(manapi::json_mask &&n) noexcept {
//     this->information = std::move(n.information);
//     this->enabled = std::exchange(n.enabled, false);
//     this->complete = std::exchange(n.complete, false);
//     return *this;
// }

manapi::json_mask::json_mask(json_mask &&n) noexcept {
    this->information = std::move(n.information);
    this->enabled = std::exchange(n.enabled, false);
    this->complete = std::exchange(n.complete, false);
}

manapi::json_mask::json_mask(const nullptr_t &n)
{
    this->enabled = false;
}

manapi::json_mask::json_mask(const json_mask &n) {
    this->information = n.information;
    this->enabled = n.enabled;
    this->complete = n.complete;
}

manapi::json_mask::~json_mask() = default;

bool manapi::json_mask::is_enabled() const {
    return this->enabled;
}

void manapi::json_mask::set_enabled(bool status) {
    enabled = status;
}

manapi::error::status manapi::json_mask::valid(const manapi::json &obj) const
{
    if (!this->enabled)
    {
        THROW_MANAPIHTTP_JSON_ERROR2(ERR_JSON_MASK_VERIFY_FAILED, "json_mask is not enabled to valid the object.");
    }

    try {
        std::vector<std::string_view> p;
        return recursive_valid (obj, this->information, true, &p);
    }
    catch (std::exception const &e) {
        return error::status_aborted("json_mask: unexpected exception",
            {{"msg", e.what()}});
    }
}

manapi::error::status manapi::json_mask::valid(const std::map<std::string, std::string> &obj) const
{
    if (!this->enabled) {
        return error::status_aborted("json_mask: it's not enabled to valid the object.");
    }

    try {
        std::vector<std::string_view> p;
        return recursive_valid (json{obj}, this->information, true, &p);
    }
    catch (std::exception const &e) {
        return error::status_aborted("json_mask: unexpected exception",
            {{"msg", e.what()}});
    }
}

const manapi::json & manapi::json_mask::get_api_tree() const {
    return this->information;
}

void manapi::json_mask::set_api_tree(json tree) {
    this->information = std::move(tree);
    this->enabled = true;
}

manapi::json manapi::json_mask::OR(json data, bool none) {
    json prepared = {
        {"none", none},
        {"obj", json::array()}
    };
    _set_status_prepared (prepared);
    if (data.is_pair())
        data = manapi::json::array({std::move(data)});
    for (auto item: data.each()) {
        initial_resolve_information(item);
        if (!none && item["none"].as_bool()) {
            none = true;
            prepared["none"] = true;
        }
        if (item["obj"].is_array()) {
            // if it also have multiple types
            prepared["obj"].push_back(item["obj"].begin<json::ARRAY>(),
                item["obj"].end<json::ARRAY>());
        }
        else {
            prepared["obj"].push_back(std::move(item["obj"]));
        }
    }
    return std::move(prepared);
}

manapi::json manapi::json_mask::ARRAY(json data, ssize_t min, ssize_t max, bool none) {
    initial_resolve_information(data);

    json prepared = {
        {"default", std::move(data["obj"])},
        {"type", static_cast<int>(json::type_array)}
    };

    if (min >= 0)
        prepared.insert({"min_mean", manapi::json::array({min, true})});

    if (max >= 0)
        prepared.insert({"max_mean", manapi::json::array({max, true})});

    prepared = {
        {"obj", std::move(prepared)},
        {"none", none}
    };
    _set_status_prepared (prepared);
    return std::move(prepared);
}

manapi::json manapi::json_mask::ARRAY(json data, bool none) {
    return json_mask::ARRAY(std::move(data), -1, -1, none);
}

void manapi::json_mask::set_complete_status(bool complete) {
    this->complete = complete;
}

void manapi::json_mask::_set_status_prepared(json &data) {
    data["__manapi_prepared"] = true;
}

void manapi::json_mask::_insert_meta_row(json &information, const std::string &key, const json &value) {
    if (information.is_object())
    {
        information.insert(key, value);
    }
    else
    {
        // array
        information[information.size() - 1].insert(key, value);
    }
}

void manapi::json_mask::initial_resolve_information(manapi::json &obj)
{
    {
        if (obj.is_object() && obj.contains("__manapi_prepared") && obj["__manapi_prepared"] == true) {
            obj.erase("__manapi_prepared");
            return;
        }
    }
    if (obj.is_string())
    {
        auto &str = obj.as_string();

        // if {x}
        if (str.size() <= 2
                || *str.begin() != '{'
                || *str.rbegin() != '}')
        {
            obj = {
                {
                    "obj",
                    {
                        {"type", static_cast<int>(json::type_string)},
                        {"value", str}
                    }
                },
                {
                    "none",
                    false
                }
            };
            return;
        }

        bool none = false;
        size_t i = 1;
        size_t m = str.size() - 1;

        json parsed = json::object();

        repeat:

        std::string type;

        bool bracket = false;
        bool square_bracket = false;
        bool quotes = false;

        // calc type
        for (; i < m; i++)
        {
            if (std::isalpha(str.at(i)))
            {
                type += str.at(i);
                continue;
            }

            break;
        }

        bool special_type = false;
        ssize_t ntype;

        // check types
        if (type == "string")
        {
            ntype = json::type_string;
        }
        else if (type == "integer")
        {
            ntype = json::type_integer;
        }
        else if (type == "decimal")
        {
            ntype = json::type_decimal;
        }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
        else if (type == "bigint")
        {
            ntype = json::type_bigint;
        }
#endif
        else if (type == "bool")
        {
            ntype = json::type_boolean;
        }
        else if (type == "null")
        {
            ntype = json::type_null;
        }
        else if (type == "number")
        {
            ntype = json::type_number;
        }
        else if (type == "any")
        {
            ntype = MANAPIHTTP_JSON_ANY;
        }
        else if (type == "none") {
            none = true;
            special_type = true;
        }
        else
        {
            THROW_MANAPIHTTP_JSON_ERROR (ERR_JSON_MASK_VERIFY_FAILED, "Could not resolve type for this expression: {}", unicode::escape_string(str));
        }

        if (!special_type) {
            if (parsed.is_object())
            {
                parsed.insert("type", ntype);
            }
            else
            {
                parsed.push_back({
                    {"type", ntype}
                });
            }
        }

        json parsed_buff;

        json_builder builder;

        // compare type (=, >=, <=, >, <)
        char compare_type = MANAPIHTTP_MASK_COMPARE_NONE;

        // calc params
        for (; i < m; i++)
        {
            char c = str.at(i);

            if (bracket || square_bracket)
            {
                if (c == '"' && ntype == json::type_string) {
                    if (!quotes) {
                        if (!builder.is_empty()) {
                            THROW_MANAPIHTTP_JSON_ERROR (ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                        }
                    }
                    builder << '"';
                    if (quotes) {
                        if (builder.is_ready()) {
                            auto res = builder.get();
                            res.unwrap();
                            _insert_meta_row(parsed, "value", res.unwrap());
                            quotes = false;
                        }
                    }
                    else {
                        quotes = true;
                    }
                    continue;
                }
                if (quotes) {
                    builder << c;
                }
                else {
                    // allows only <=, >= and =
                    for (; i < m; i++)
                    {
                        c = str.at(i);

                        if (c == '=')
                        {
                            if (compare_type == MANAPIHTTP_MASK_COMPARE_GREATER)
                            {
                                compare_type = MANAPIHTTP_MASK_COMPARE_EQUAL_OR_GREATER;
                            }
                            else if (compare_type == MANAPIHTTP_MASK_COMPARE_LESS)
                            {
                                compare_type = MANAPIHTTP_MASK_COMPARE_EQUAL_OR_LESS;
                            }
                            else if (compare_type == MANAPIHTTP_MASK_COMPARE_NONE)
                            {
                                compare_type = MANAPIHTTP_MASK_COMPARE_EQUAL;
                            }
                            else
                            {
                                THROW_MANAPIHTTP_JSON_ERROR (ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                            }
                        }
                        else if (c == '>')
                        {
                            if (compare_type != MANAPIHTTP_MASK_COMPARE_EQUAL && compare_type != MANAPIHTTP_MASK_COMPARE_NONE)
                            {
                                THROW_MANAPIHTTP_JSON_ERROR (ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                            }

                            compare_type = MANAPIHTTP_MASK_COMPARE_GREATER;
                        }
                        else if (c == '<')
                        {
                            if (compare_type != MANAPIHTTP_MASK_COMPARE_EQUAL && compare_type != MANAPIHTTP_MASK_COMPARE_NONE)
                            {
                                THROW_MANAPIHTTP_JSON_ERROR (ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                            }

                            compare_type = MANAPIHTTP_MASK_COMPARE_LESS;
                        }
                        else
                        {
                            break;
                        }
                    }


                    if ((bracket && c == ')') || (square_bracket && c == ']'))
                    {

                        if (bracket)
                        {
                            bracket = false;
                        }

                        if (square_bracket)
                        {
                            square_bracket = false;
                        }

                        if (builder.is_empty())
                        {
                            continue;
                        }

                    }
                    else if (c != ' ')
                    {
                        builder << c;
                        continue;
                        //THROW_MANAPIHTTP_JSON_ERROR (ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                    }

                    if (builder.is_empty()) {
                        continue;
                    }

                    // calc size

                    if (ntype == json::type_decimal)
                    {
                        parsed_buff = builder.get().unwrap().as_decimal_cast();
                    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
                    else if (ntype == json::type_bigint)
                    {
                        parsed_buff = builder.get().unwrap().as_bigint_cast();
                    }
#endif
                    else if (ntype == json::type_boolean)
                    {
                        parsed_buff = builder.get().unwrap().as_bool_cast();
                    }
                    else
                    {
                        // others
                        parsed_buff = builder.get().unwrap().as_integer_cast();
                    }

                    switch (compare_type) {
                        case MANAPIHTTP_MASK_COMPARE_NONE:
                            // its value
                            _insert_meta_row (parsed, "value", parsed_buff);
                        break;
                        case MANAPIHTTP_MASK_COMPARE_EQUAL:
                            _insert_meta_row (parsed, "max_mean", manapi::json::array({parsed_buff, true}));
                            _insert_meta_row (parsed, "min_mean", manapi::json::array({parsed_buff, true}));
                        break;
                        case MANAPIHTTP_MASK_COMPARE_EQUAL_OR_LESS:
                            _insert_meta_row (parsed, "max_mean", manapi::json::array({parsed_buff, true}));
                        break;
                        case MANAPIHTTP_MASK_COMPARE_LESS:
                            _insert_meta_row (parsed, "max_mean", manapi::json::array({parsed_buff, false}));
                        break;
                        case MANAPIHTTP_MASK_COMPARE_EQUAL_OR_GREATER:
                            _insert_meta_row (parsed, "min_mean", manapi::json::array({parsed_buff, true}));
                        break;
                        case MANAPIHTTP_MASK_COMPARE_GREATER:
                            _insert_meta_row (parsed, "min_mean", manapi::json::array({parsed_buff, false}));
                        break;
                        default:
                            THROW_MANAPIHTTP_JSON_ERROR (ERR_JSON_MASK_VERIFY_FAILED, "Bug has been detected: {}", "compare type has invalid value");
                    }

                    // clean up
                    builder.clear();
                    compare_type = -1;
                }
            }

            else if (c == '(')
            {
                if (ntype == json::type_array || ntype == json::type_object)
                {
                    THROW_MANAPIHTTP_JSON_ERROR (ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
                }
                bracket = true;
                builder.clear();
            }

            else if (c == '[')
            {
                // change type to array
                ntype = json::type_array;

                parsed = {
                    {"type", ntype},
                    {"default", std::move(parsed)}
                };

                square_bracket = true;
                builder.clear();
            }

            else if (c == '|')
            {
                i++;

                // resolve object -> array
                if (parsed.is_object())
                {
                    json arr = json::array();
                    arr.push_back(parsed);

                    parsed = arr;
                }

                goto repeat;
            }

            else
            {
                THROW_MANAPIHTTP_JSON_ERROR (ERR_JSON_MASK_VERIFY_FAILED, "Invalid symbol at {}: {}", i, c);
            }
        }

        end:

        if (!builder.is_empty()) {
            THROW_MANAPIHTTP_JSON_ERROR(ERR_JSON_UNEXPECTED_END, "Unexpected end at {}", m);
        }

        obj = {
            {"obj", std::move(parsed)},
            {"none", none}
        };
    }
    else if (obj.is_object())
    {
        for (auto it = obj.begin<json::OBJECT>(); it != obj.end<json::OBJECT>(); it++)
        {
            initial_resolve_information(it->second);
        }

        obj = {
            {
                "obj", {
                    {"type", static_cast<int>(json::type_object)},
                    {"value", std::move(obj)}
                },
            },
            {"none", false}
        };
    }
    else if (obj.is_array())
    {
        for (auto it = obj.begin<json::ARRAY>(); it != obj.end<json::ARRAY>(); ++it)
        {
            initial_resolve_information(*it);
        }

        obj = {
            {
                "obj", {
                        {"type", static_cast<int>(json::type_array)},
                        {"value", std::move(obj)}
                },
            },
            {"none", false}
        };
    }
    else {
        obj = {
            {
                "obj", {
                        {"type", static_cast<int>(obj.data_type())},
                        {"value", std::move(obj)}
                },
            },
            {"none", false}
        };
    }
}

template<typename T>
manapi::error::status default_compare_information(const T &val, const manapi::json &information, std::vector<std::string_view> *path) {
    auto &p = information.as_object();
    auto fit = p.find("min_mean");
    if (fit != p.end())
    {
        if (!json_verify_min_mean<T>(fit->second, val))
            return manapi::error::status_invalid_argument("json_mask: value is lower or equals min_mean", jm_msg_with_path_and_param(path, fit->second));
    }

    fit = p.find("max_mean");
    if (fit != p.end())
    {

        if (!json_verify_max_mean<T>(fit->second, val))
            return manapi::error::status_invalid_argument("json_mask: value is greater or equals max_mean", jm_msg_with_path_and_param(path, fit->second));
    }

    return manapi::error::status_ok();
}

manapi::error::status manapi::json_mask::recursive_valid(const manapi::json &obj, const manapi::json &item, bool is_complex, std::vector<std::string_view> *path) const {
    const auto &information = is_complex ? item["obj"] : item;

    if (information.is_array())
    {
        // is an array
        for (auto it = information.begin<json::ARRAY>(); it != information.end<json::ARRAY>(); ++it)
        {
            try {
                auto res = recursive_valid(obj, *it, false, path);
                if (res.ok())
                {
                    return std::move(res);
                }
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG("json_mask: unexpected bug: ", e.what());
            }
        }

        return error::status_invalid_argument("json_mask: no match for array", jm_msg_with_path(path));
    }

    // information is a map

    auto &type = information["type"].as_integer();

    if (type == json::type_string)
    {
        // invalid type
        if (!obj.is_string())
        {
            return error::status_invalid_argument("json_mask: must be a string", jm_msg_with_path(path));
        }

        auto res = default_compare_information<size_t> (obj.size(), information, path);
        if (!res.ok())
        {
            return std::move(res);
        }

        auto &o = information.as_object();
        auto oit = o.find("value");

        // by value ex: STR1 != STR2
        if (oit != o.end()) {
            if (oit->second.as_string() != obj.as_string())
                return error::status_invalid_argument("json_mask: strings are not match", jm_msg_with_path_and_param(path, obj.as_string()));
        }

        // ex: {string}
        return error::status_ok();
    }
    if (type == json::type_null)
    {
        // invalid type
        if (!obj.is_null())
            return error::status_invalid_argument("json_mask: must be a null", jm_msg_with_path(path));
        return error::status_ok();
    }
    if (type == json::type_boolean)
    {
        // invalid type
        if (!obj.is_bool())
        {
            return error::status_invalid_argument("json_mask: must be a bool", jm_msg_with_path(path));
        }

        auto &o = information.as_object();
        auto oit = o.find("value");

        // false / true
        if (oit != o.end())
        {
            bool const p = oit->second.as_bool();
            if (obj.as_bool() != p)
                return error::status_invalid_argument("json_mask: value aren't match", jm_msg_with_path_and_param(path, p));
        }

        oit = o.find("mean");
        if (oit != o.end())
        {
            bool const p = oit->second.as_bool();
            if (obj.as_bool() != p)
                return error::status_invalid_argument("json_mask: mean aren't match", jm_msg_with_path_and_param(path, p));
        }

        // ex: {bool}
        return error::status_ok();
    }
    if (type == json::type_integer)
    {
        // invalid type
        if (!obj.is_integer())
        {
            return error::status_invalid_argument("json_mask: must be an integer", jm_msg_with_path(path));
        }

        // invalid value
        auto &o = information.as_object();
        auto fit = o .find("value");
        if (fit != o.end())
        {
            auto const p = fit->second.as_integer();
            if (obj.as_integer() != p)
                return error::status_invalid_argument("json_mask: value aren't match", jm_msg_with_path_and_param(path, p));
        }

        fit = o.find("mean");
        if (fit != o.end())
        {
            auto const p = fit->second.as_integer();
            if (obj.as_integer() != p)
                return error::status_invalid_argument("json_mask: mean aren't match", jm_msg_with_path_and_param(path, p));
        }

        auto res = default_compare_information<ssize_t> (obj.as_integer(), information, path);
        if (!res.ok())
        {
            return std::move(res);
        }

        // ex: {number}
        return error::status_ok();
    }
    if (type == json::type_decimal)
    {
        // invalid type
        if (!obj.is_decimal())
        {
            return error::status_invalid_argument("json_mask: must be a decimal", jm_msg_with_path(path));
        }


        auto res = default_compare_information<long double> (obj.as_decimal(), information, path);
        if (!res.ok())
        {
            return std::move(res);
        }

        // invalid value
        auto &o = information.as_object();
        auto fit = o.find("value");
        if (fit != o.end())
        {
            auto const p = fit->second.as_decimal();
            if (obj.as_decimal() != p)
                return error::status_invalid_argument("json_mask: value aren't match", jm_msg_with_path_and_param(path, p));
        }

        fit = o.find("mean");
        if (fit != o.end())
        {
            auto const p = fit->second.as_decimal();
            if (obj.as_decimal() != p)
                return error::status_invalid_argument("json_mask: mean aren't match", jm_msg_with_path_and_param(path, p));
        }

        // ex: {decimal}
        return error::status_ok();
    }
#ifdef MANAPIHTTP_BIGINT_SUPPORT
    if (type == json::type_bigint) {
        // invalid type
        if (!obj.is_bigint()) {
            return error::status_invalid_argument("json_mask: must be a bigint", jm_msg_with_path(path));
        }

        auto res = default_compare_information<bigint> (obj.as_bigint(), information, path);
        if (!res.ok())
        {
            return std::move(res);
        }

        auto &o = obj.as_object();
        auto fit = o.find("value");
        // invalid value
        if (fit != o.end()) {
            auto &p = fit->second.as_bigint();
            if (obj.as_bigint() != p)
                return error::status_invalid_argument("json_mask: value aren't match", jm_msg_with_path_and_param(path, p.stringify()));
        }

        fit = o.find("mean");
        if (fit != o.end()) {
            auto &p = fit->second.as_bigint();
            if (obj.as_bigint() != p)
                return error::status_invalid_argument("json_mask: mean aren't match", jm_msg_with_path_and_param(path, p.stringify()));
        }

        // ex: {bigint}
        return error::status_ok();
    }
#endif
    if (type == json::type_number) {
        if (
#ifdef MANAPIHTTP_BIGINT_SUPPORT
            !obj.is_bigint() &&
#endif
            !obj.is_integer() && !obj.is_decimal()) {
            return error::status_invalid_argument("json_mask: must be a number", jm_msg_with_path(path));
        }

        return error::status_ok();
    }
    if (type == MANAPIHTTP_JSON_ANY) {
        return error::status_ok();
    }
    if (type == json::type_object) {
        if (!obj.is_object()) {
            return error::status_invalid_argument("json_mask: must be an object", jm_msg_with_path(path));
        }

        auto res = default_compare_information<size_t> (obj.size(), information, path);
        if (!res.ok())
        {
            return std::move(res);
        }

        auto &o = information.as_object();
        auto oit = o.find("value");
        if (oit != o.end())
        {
            auto &data = oit->second;

            if (obj.size() > data.size())
            {
                return error::status_invalid_argument("json_mask: size is too large", jm_msg_with_path_and_param(path, data.size()));
            }

            if (this->complete) {
                auto &p = obj.as_object();
                for (const auto &it : data.entries())
                {
                    auto fit = p.find(it.first);
                    // incorrect key
                    if (fit == p.end())
                    {
                        if (it.second["none"].as_bool()) { continue; }
                        return error::status_invalid_argument("json_mask: item not exists", jm_msg_with_path_and_param(path, it.first));
                    }
                    if (path)
                        path->push_back(it.first);
                    // incorrect value
                    res = recursive_valid(fit->second, it.second, true, path);
                    if (!res.ok())
                        return res;
                    if (path)
                        path->pop_back();
                }
            }
            else {
                auto &p = data.as_object();
                for (const auto &it : obj.entries()) {
                    auto fit = p.find(it.first);
                    // dont exists
                    if (fit == p.end())
                        return error::status_invalid_argument("json_mask: item not exists", jm_msg_with_path_and_param(path,it.first));
                    if (path)
                        path->push_back(it.first);
                    // incorrect value
                    res = recursive_valid(it.second, fit->second, true, path);
                    if (!res.ok())
                        return res;
                    if (path)
                        path->pop_back();
                }
            }
        }

        // all ok
        return error::status_ok();
    }
    if (type == json::type_array)
    {
        if (!obj.is_array())
        {
            return error::status_invalid_argument("json_mask: must be an array", jm_msg_with_path(path));
        }

        auto res = default_compare_information<size_t> (obj.size(), information, path);
        if (!res.ok())
        {
            return std::move(res);
        }

        auto &o = information.as_object();
        auto oit = o.find("value");

        if (oit != o.end()) {

            auto &data = oit->second;

            if (obj.size() != data.size()) {
                return manapi::error::status_invalid_argument("json_mask: size not match", jm_msg_with_path_and_param(path, data.size()));
            }

            for (size_t i = 0; i < obj.size(); i++)
            {
                res = recursive_valid(obj[i], data[i], true, path);
                if (!res.ok())
                {
                    return res;
                }
            }
        }

        oit = o.find("default");
        if (oit != o.end())
        {
            // we need to validate all items as 'type' (from type[...] or type(...)[...])

            auto &default_ = oit->second;

            for (size_t i = 0; i < obj.size(); i++) {
                if (path)
                    path->emplace_back(nullptr, i+1);
                res = recursive_valid(obj.at(i), default_, false, path);
                if (!res.ok())
                {
                    return res;
                }

                if (path)
                    path->pop_back();
            }
        }

        return error::status_ok();
    }

    if (type == MANAPIHTTP_JSON_NONE) {
        return error::status_ok();
    }

    return error::status_invalid_argument("json_mask: invalid json data type");
}
