#ifndef MANAPIJSONWEBTOKENTRAITS_HPP
#define MANAPIJSONWEBTOKENTRAITS_HPP

#include <jwt-cpp/jwt.h>
#include "../ManapiJson.hpp"
#include "../ManapiUtils.hpp"

namespace jwt {
    namespace traits {
        struct manapi_json {
            using value_type = manapi::json;
            using object_type = manapi::json::OBJECT;
            using array_type = manapi::json::ARRAY;
            using string_type = manapi::json::STRING;
            using number_type = manapi::json::DECIMAL;
            using integer_type = manapi::json::NUMBER;
            using boolean_type = manapi::json::BOOLEAN;
            static jwt::json::type get_type(const value_type& val) {
                using jwt::json::type;
                if (val.is_bool()) return type::boolean;
                if (val.is_number()) return type::integer;
                if (val.is_bigint()) return type::number; /** bigint can be decimal */
                if (val.is_decimal()) return type::number;
                if (val.is_string()) return type::string;
                if (val.is_array()) return type::array;
                if (val.is_object()) return type::object;

                throw std::logic_error("invalid type");
            }

            static object_type as_object(const value_type& val) {
                if (!val.is_object()) throw std::bad_cast();
                return val.as_object();
            }

            static string_type as_string(const value_type& val) {
                if (!val.is_string()) throw std::bad_cast();
                return val.as_string();
            }

            static array_type as_array(const value_type& val) {
                if (!val.is_array()) throw std::bad_cast();
                return val.as_array();
            }

            static integer_type as_integer(const value_type& val) {
                if (!val.is_number()) throw std::bad_cast();
                return val.as_number();
            }

            static boolean_type as_boolean(const value_type& val) {
                if (!val.is_bool()) throw std::bad_cast();
                return val.as_bool();
            }

            static number_type as_number(const value_type& val) {
                if (!val.is_decimal()) throw std::bad_cast();
                return static_cast<double> (val.as_decimal());
            }

            static bool parse(value_type& val, const std::string& str) {
                try {
                    val = manapi::json (str, true);
                    return true;
                }
                catch (std::exception const &e) {
                    MANAPI_LOG ("failed to parse: {}", e.what());
                }

                return false;
            }

            static std::string serialize(const value_type& val) { return val.dump(); }
        };
    } // namespace traits
} // namespace jwt

#endif //MANAPIJSONWEBTOKENTRAITS_HPP
