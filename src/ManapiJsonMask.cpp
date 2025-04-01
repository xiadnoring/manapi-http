#include "ManapiJsonMask.hpp"

#include <utility>

#include "ManapiDebug.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJsonBuilder.hpp"
#include "ManapiUnicode.hpp"

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

void manapi::json_mask::set_enabled(const bool &status) {
    enabled = status;
}

bool manapi::json_mask::valid(const manapi::json &obj) const
{
    if (!this->enabled)
    {
        THROW_MANAPIHTTP_JSON_ERROR2(ERR_JSON_MASK_VERIFY_FAILED, "json_mask is not enabled to valid the object.");
    }

    try {
        return recursive_valid (obj, this->information);
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG2(e.what());
    }
    return false;
}

bool manapi::json_mask::valid(const std::map<std::string, std::string> &obj) const
{
    if (!this->enabled)
    {
        THROW_MANAPIHTTP_JSON_ERROR2(ERR_JSON_MASK_VERIFY_FAILED, "json_mask is not enabled to valid the object.");
    }
    try {
        return recursive_valid (json{obj}, this->information);
    }
    catch (std::exception const &e) {
        MANAPIHTTP_LOG2(e.what());
    }
    return false;
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

manapi::json manapi::json_mask::ARRAY(json data, bool none) {
    initial_resolve_information(data);


    json prepared = {
        {"obj", {
            {"default", std::move(data["obj"])},
            {"type", static_cast<int>(json::type_array)}
        }},
        {"none", none}
    };
    _set_status_prepared (prepared);
    return std::move(prepared);
}

void manapi::json_mask::set_complete_status(const bool &complete) {
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
        else if (type == "bigint")
        {
            ntype = json::type_bigint;
        }
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
                            _insert_meta_row(parsed, "value", builder.get());
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
                            // change type to array
                            ntype = json::type_array;

                            parsed = {
                                {"type", ntype},
                                {"default", parsed}
                            };

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
                        parsed_buff = builder.get().as_decimal_cast();
                    }
                    else if (ntype == json::type_bigint)
                    {
                        parsed_buff = builder.get().as_bigint_cast();
                    }
                    else if (ntype == json::type_boolean)
                    {
                        parsed_buff = builder.get().as_bool_cast();
                    }
                    else
                    {
                        // others
                        parsed_buff = builder.get().as_integer_cast();
                    }

                    switch (compare_type) {
                        case MANAPIHTTP_MASK_COMPARE_NONE:
                            // its value
                            _insert_meta_row (parsed, "value", parsed_buff);
                        break;
                        case MANAPIHTTP_MASK_COMPARE_EQUAL:
                            _insert_meta_row (parsed, "max_mean", parsed_buff + 1);
                            _insert_meta_row (parsed, "min_mean", parsed_buff - 1);
                        break;
                        case MANAPIHTTP_MASK_COMPARE_EQUAL_OR_LESS:
                            parsed_buff = parsed_buff + 1;
                        case MANAPIHTTP_MASK_COMPARE_LESS:
                            _insert_meta_row (parsed, "max_mean", parsed_buff);
                        break;
                        case MANAPIHTTP_MASK_COMPARE_EQUAL_OR_GREATER:
                            parsed_buff = parsed_buff - 1;
                        case MANAPIHTTP_MASK_COMPARE_GREATER:
                            _insert_meta_row (parsed, "min_mean", parsed_buff);
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
}

bool manapi::json_mask::recursive_valid(const manapi::json &obj, const manapi::json &item, const bool &is_complex) const {
    const auto &information = is_complex ? item["obj"] : item;

    if (information.is_array())
    {
        // is an array
        for (auto it = information.begin<json::ARRAY>(); it != information.end<json::ARRAY>(); it++)
        {
            try {
                if (recursive_valid(obj, *it, false))
                {
                    return true;
                }
            }
            catch (std::exception const &e) {
                MANAPIHTTP_LOG2(e.what());
            }
        }

        return false;
    }

    // information is a map

    auto &type = information["type"].as_integer();

    if (type == json::type_string)
    {
        // invalid type
        if (!obj.is_string())
        {
            return false;
        }

        if (!default_compare_information (obj, information))
        {
            return false;
        }

        // by value ex: STR1 != STR2
        if (information.contains("value"))
        {
            return information["value"].as_string() == obj.as_string();
        }

        // ex: {string}
        return true;
    }
    if (type == json::type_null)
    {
        // invalid type
        return obj.is_null();
    }
    if (type == json::type_boolean)
    {
        // invalid type
        if (!obj.is_bool())
        {
            return false;
        }

        // false / true
        if (information.contains("value"))
        {
            return obj.as_bool() == information["value"].as_bool();
        }

        if (information.contains("mean"))
        {
            return obj.as_bool() == information["mean"].as_bool();
        }

        // ex: {bool}
        return true;
    }
    if (type == json::type_integer)
    {
        // invalid type
        if (!obj.is_integer())
        {
            return false;
        }

        // invalid value
        if (information.contains("value"))
        {
            return obj.as_integer() == information["value"].as_integer();
        }

        if (information.contains("mean"))
        {
            return obj.as_integer() == information["mean"].as_integer();
        }

        // ex: {number}
        return true;
    }
    if (type == json::type_decimal)
    {
        // invalid type
        if (!obj.is_decimal())
        {
            return false;
        }

        // invalid value
        if (information.contains("value"))
        {
            return obj.as_decimal() == information["value"].as_decimal();
        }

        if (information.contains("mean"))
        {
            return obj.as_decimal() == information["mean"].as_decimal();
        }

        // ex: {decimal}
        return true;
    }
    if (type == json::type_bigint)
    {
        // invalid type
        if (!obj.is_bigint())
        {
            return false;
        }

        // invalid value
        if (information.contains("value"))
        {
            return obj.as_bigint() == information["value"].as_bigint();
        }

        if (information.contains("mean"))
        {
            return obj.as_bigint() == information["mean"].as_bigint();
        }

        // ex: {bigint}
        return true;
    }
    if (type == json::type_number)
    {
        if (!obj.is_bigint() && !obj.is_integer() && !obj.is_decimal())
        {
            return false;
        }

        return true;
    }
    if (type == MANAPIHTTP_JSON_ANY)
    {
        return true;
    }
    if (type == json::type_object)
    {
        if (!obj.is_object())
        {
            return false;
        }

        if (!default_compare_information (obj, information))
        {
            return false;
        }

        if (information.contains("value"))
        {
            auto &data = information["value"];

            if (obj.size() > data.size())
            {
                return false;
            }

            if (this->complete) {
                for (const auto &it : data.entries())
                {
                    // incorrect key
                    if (!obj.contains(it.first))
                    {
                        if (it.second["none"].as_bool()) { continue; }
                        return false;
                    }
                    // incorrect value
                    if (!recursive_valid(obj.at(it.first), it.second))
                    {
                        return false;
                    }
                }
            }
            else {
                for (const auto &it : obj.entries()) {
                    // dont exists
                    if (!data.contains(it.first)) {
                        return false;
                    }
                    // incorrect value
                    if (!recursive_valid(it.second, data[it.first])) {
                        return false;
                    }
                }
            }
        }

        // all ok
        return true;
    }
    if (type == json::type_array)
    {
        if (!obj.is_array())
        {
            return false;
        }

        if (!default_compare_information (obj, information))
        {
            return false;
        }

        if (information.contains("value")) {

            auto &data = information["value"];

            if (obj.size() != data.size())
            {
                return false;
            }

            for (size_t i = 0; i < obj.size(); i++)
            {
                if (!recursive_valid(obj.at(i), data.at(i)))
                {
                    return false;
                }
            }
        }

        if (information.contains("default"))
        {
            // we need to validate all items as 'type' (from type[...] or type(...)[...])

            auto &_default = information["default"];

            for (size_t i = 0; i < obj.size(); i++)
            {
                if (!recursive_valid(obj.at(i), _default, false))
                {
                    return false;
                }
            }
        }

        return true;
    }

    if (type == MANAPIHTTP_JSON_NONE) {
        return true;
    }

    return false;
}

bool manapi::json_mask::default_compare_information(const manapi::json &obj, const manapi::json &information, const bool &by_size) {
    if (by_size)
    {
        if (information.contains("min_mean"))
        {
            if (obj.size() <= information["min_mean"].as_integer())
            {
                return false;
            }
        }

        if (information.contains("max_mean"))
        {
            if (obj.size() >= information["max_mean"].as_integer())
            {
                return false;
            }
        }
    }


    return true;
}
