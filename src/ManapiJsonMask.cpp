#include "ManapiJsonMask.hpp"
#include "ManapiUtils.hpp"

#define MANAPI_JSON_ANY (-1)
#define MANAPI_JSON_NONE (-2)

#define MANAPI_MASK_COMPARE_NONE (-1)
#define MANAPI_MASK_COMPARE_EQUAL 0
#define MANAPI_MASK_COMPARE_GREATER 1
#define MANAPI_MASK_COMPARE_LESS 2
#define MANAPI_MASK_COMPARE_EQUAL_OR_GREATER 3
#define MANAPI_MASK_COMPARE_EQUAL_OR_LESS 4

manapi::net::utils::json_mask::json_mask(const std::initializer_list<json> &data)
{
    information = data;
    initial_resolve_information (information);

    enabled = true;
}

manapi::net::utils::json_mask::json_mask(const nullptr_t &n)
{
    enabled = false;
}

manapi::net::utils::json_mask::~json_mask() = default;

bool manapi::net::utils::json_mask::is_enabled() const {
    return enabled;
}

void manapi::net::utils::json_mask::set_enabled(const bool &status) {
    enabled = status;
}

bool manapi::net::utils::json_mask::valid(const manapi::net::utils::json &obj) const
{
    if (!enabled)
    {
        THROW_MANAPI_EXCEPTION("{}", "json_mask is not enabled to valid the object.");
    }

    return recursive_valid (obj, information);
}

bool manapi::net::utils::json_mask::valid(const std::map<std::string, std::string> &obj) const
{
    if (!enabled)
    {
        THROW_MANAPI_EXCEPTION("{}", "json_mask is not enabled to valid the object.");
    }

    json a = json::object();

    for (const auto &it: obj)
    {
        a.insert(it.first, it.second);
    }

    return recursive_valid (a, information);
}

void manapi::net::utils::json_mask::initial_resolve_information(manapi::net::utils::json &obj)
{
    if (obj.is_string())
    {
        auto *str = obj.get_ptr<std::string>();

        // if {x}
        if (str->size() <= 2
                || *str->begin() != '{'
                || *str->rbegin() != '}')
        {
            obj = {
                    {"type", json::type_string},
                    {"value", *str}
            };

            return;
        }

        bool none = false;
        size_t i = 1;
        size_t m = str->size() - 1;

        json parsed = json::object();

        repeat:

        std::string type;

        bool bracket = false;
        bool square_bracket = false;

        // calc type
        for (; i < m; i++)
        {
            if (std::isalpha(str->at(i)))
            {
                type += str->at(i);
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
        else if (type == "number")
        {
            ntype = json::type_number;
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
        else if (type == "numeric")
        {
            ntype = json::type_numeric;
        }
        else if (type == "any")
        {
            ntype = MANAPI_JSON_ANY;
        }
        else if (type == "none") {
            none = true;
            special_type = true;
        }
        else
        {
            THROW_MANAPI_EXCEPTION ("Could not resolve type for this expression: {}", escape_string(*str));
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

        std::string buff;
        json        parsed_buff;

        // compare type (=, >=, <=, >, <)
        char        compare_type = MANAPI_MASK_COMPARE_NONE;

        // calc params
        for (; i < m; i++)
        {
            char c = str->at(i);

            if (bracket || square_bracket)
            {
                // allows only <=, >= and =
                for (; i < m; i++)
                {
                    c = str->at(i);

                    if (c == '=')
                    {
                        if (compare_type == MANAPI_MASK_COMPARE_GREATER)
                        {
                            compare_type = MANAPI_MASK_COMPARE_EQUAL_OR_GREATER;
                        }
                        else if (compare_type == MANAPI_MASK_COMPARE_LESS)
                        {
                            compare_type = MANAPI_MASK_COMPARE_EQUAL_OR_LESS;
                        }
                        else if (compare_type == MANAPI_MASK_COMPARE_NONE)
                        {
                            compare_type = MANAPI_MASK_COMPARE_EQUAL;
                        }
                        else
                        {
                            THROW_MANAPI_EXCEPTION ("Invalid symbol at {}: {}", i, c);
                        }
                    }
                    else if (c == '>')
                    {
                        if (compare_type != MANAPI_MASK_COMPARE_EQUAL && compare_type != MANAPI_MASK_COMPARE_NONE)
                        {
                            THROW_MANAPI_EXCEPTION ("Invalid symbol at {}: {}", i, c);
                        }

                        compare_type = MANAPI_MASK_COMPARE_GREATER;
                    }
                    else if (c == '<')
                    {
                        if (compare_type != MANAPI_MASK_COMPARE_EQUAL && compare_type != MANAPI_MASK_COMPARE_NONE)
                        {
                            THROW_MANAPI_EXCEPTION ("Invalid symbol at {}: {}", i, c);
                        }

                        compare_type = MANAPI_MASK_COMPARE_LESS;
                    }
                    else
                    {
                        break;
                    }
                }

                if (std::isdigit(c) || c == '.')
                {
                    buff += c;
                    continue;
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

                    if (buff.empty())
                    {
                        continue;
                    }

                }
                else if (c != ' ')
                {
                    THROW_MANAPI_EXCEPTION ("Invalid symbol at {}: {}", i, c);
                }

                // calc size

                if (ntype == json::type_decimal)
                {
                    parsed_buff = std::stold(buff);
                }
                else if (ntype == json::type_bigint)
                {
                    parsed_buff = bigint(buff);
                }
                else if (ntype == json::type_boolean)
                {
                    parsed_buff = bool(std::stold(buff));
                }
                else
                {
                    // others
                    parsed_buff = std::stoll(buff);
                }

                std::string key;

                switch (compare_type) {
                    case MANAPI_MASK_COMPARE_NONE:
                    case MANAPI_MASK_COMPARE_EQUAL:
                        key = "mean";
                        break;
                    case MANAPI_MASK_COMPARE_EQUAL_OR_LESS:
                        parsed_buff = parsed_buff + 1;
                    case MANAPI_MASK_COMPARE_LESS:
                        key = "max_mean";
                        break;
                    case MANAPI_MASK_COMPARE_EQUAL_OR_GREATER:
                        parsed_buff = parsed_buff - 1;
                    case MANAPI_MASK_COMPARE_GREATER:
                        key = "min_mean";
                        break;
                    default:
                        THROW_MANAPI_EXCEPTION ("Bug has been detected: {}", "compare type has invalid value");
                }

                if (parsed.is_object())
                {
                    parsed.insert(key, parsed_buff);
                }
                else
                {
                    // array
                    parsed[parsed.size() - 1].insert(key, parsed_buff);
                }

                // clean up
                buff = "";
                compare_type = -1;
            }

            else if (c == '(')
            {
                if (ntype == json::type_array || ntype == json::type_object)
                {
                    THROW_MANAPI_EXCEPTION ("Invalid symbol at {}: {}", i, c);
                }
                bracket = true;
            }

            else if (c == '[')
            {
                if (ntype == json::type_array || ntype == json::type_object)
                {
                    THROW_MANAPI_EXCEPTION ("Invalid symbol at {}: {}", i, c);
                }
                square_bracket = true;
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
                THROW_MANAPI_EXCEPTION ("Invalid symbol at {}: {}", i, c);
            }
        }

        end:

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
                    {"type", json::type_object},
                    {"data", std::move(obj)}
                },
            },
            {"none", false}
        };
    }
    else if (obj.is_array())
    {
        for (auto it = obj.begin<json::ARRAY>(); it != obj.end<json::ARRAY>(); it++)
        {
            initial_resolve_information(*it);
        }

        obj = {
            {
                "obj", {
                        {"type", json::type_object},
                        {"data", std::move(obj)}
                },
            },
            {"none", false}
        };
    }
}

bool manapi::net::utils::json_mask::recursive_valid(const manapi::net::utils::json &obj, const manapi::net::utils::json &item, const bool &is_complex) {
    const auto &information = is_complex ? item["obj"] : item;

    if (information.is_array())
    {
        // is an array
        for (auto it = information.begin<json::ARRAY>(); it != information.end<json::ARRAY>(); it++)
        {
            if (recursive_valid(obj, *it, false))
            {
                return true;
            }
        }

        return false;
    }

    // information is a map

    auto &type = information["type"].get<ssize_t>();

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
            return *information["value"].get_ptr<std::string>() == *obj.get_ptr<std::string>();
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
            return *obj.get_ptr<bool>() == *information["value"].get_ptr<bool>();
        }

        if (information.contains("mean"))
        {
            return *obj.get_ptr<bool>() == *information["mean"].get_ptr<bool>();
        }

        // ex: {bool}
        return true;
    }
    if (type == json::type_number)
    {
        // invalid type
        if (!obj.is_number())
        {
            return false;
        }

        // invalid value
        if (information.contains("value"))
        {
            return *obj.get_ptr<ssize_t>() == *information["value"].get_ptr<ssize_t>();
        }

        if (information.contains("mean"))
        {
            return *obj.get_ptr<ssize_t>() == *information["mean"].get_ptr<ssize_t>();
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
            return *obj.get_ptr<double long>() == *information["value"].get_ptr<double long>();
        }

        if (information.contains("mean"))
        {
            return *obj.get_ptr<double long>() == *information["mean"].get_ptr<double long>();
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
            return *obj.get_ptr<bigint>() == *information["value"].get_ptr<bigint>();
        }

        if (information.contains("mean"))
        {
            return *obj.get_ptr<bigint>() == *information["mean"].get_ptr<bigint>();
        }

        // ex: {bigint}
        return true;
    }
    if (type == json::type_numeric)
    {
        if (!obj.is_bigint() && !obj.is_number() && !obj.is_decimal())
        {
            return false;
        }

        return true;
    }
    if (type == MANAPI_JSON_ANY)
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

        if (information.contains("data"))
        {
            auto &data = information["data"];

            if (obj.size() > data.size())
            {
                return false;
            }

            for (auto it = data.begin<json::OBJECT>(); it != data.end<json::OBJECT>(); it++)
            {
                // incorrect key
                if (!obj.contains(it->first))
                {
                    return it->second["none"].get<bool>();
                }
                // incorrect value
                if (!recursive_valid(obj.at(it->first), it->second))
                {
                    return false;
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

        if (information.contains("data")) {

            auto &data = information["data"];

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
                if (!recursive_valid(obj.at(i), _default))
                {
                    return false;
                }
            }
        }

        return true;
    }

    if (type == MANAPI_JSON_NONE) {
        return true;
    }

    return false;
}

bool manapi::net::utils::json_mask::default_compare_information(const manapi::net::utils::json &obj, const manapi::net::utils::json &information, const bool &by_size) {
    if (by_size)
    {
        // by length
        if (information.contains("mean"))
        {
            if (obj.size() != *information["mean"].get_ptr<ssize_t>())
            {
                return false;
            }
        }

        if (information.contains("min_mean"))
        {
            if (obj.size() <= *information["min_mean"].get_ptr<ssize_t>())
            {
                return false;
            }
        }

        if (information.contains("max_mean"))
        {
            if (obj.size() >= *information["max_mean"].get_ptr<ssize_t>())
            {
                return false;
            }
        }
    }


    return true;
}
