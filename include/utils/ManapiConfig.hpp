#pragma once

#include "../json/ManapiJson.hpp"

namespace manapi::internal {
    class DLLExportImport config_interface {
    public:

        template<typename T>
        requires(std::is_same_v<std::string, T>)
        static std::optional<T> get_value_config_param (const manapi::json &n) {
            if (!n.is_string())
                return {};
            return n.as_string();
        }

        template<typename T>
        requires(std::is_integral_v<T> && !std::is_same_v<bool, T>)
        static std::optional<T> get_value_config_param (const manapi::json &n) {
            if (!n.is_integer())
                return {};
            return static_cast<T>(n.as_integer());
        }

        template<typename T>
        requires(std::is_same_v<T, bool>)
        static std::optional<T> get_value_config_param (const manapi::json &n) {
            if (!n.is_bool())
                return {};
            return n.as_bool();
        }

        template<typename T>
        requires(std::is_floating_point_v<T>)
        static std::optional<T> get_value_config_param (const manapi::json &n) {
            if (!n.is_decimal())
                return {};
            return static_cast<T>(n.as_decimal());
        }

        static manapi::json get_config_object_param (const manapi::json &config, std::string_view name, manapi::json value) {
            if (config.is_object()) {
                auto &obb = config.as_object();
                auto it = obb.find(name);
                if (it != obb.end()) {
                    if (it->second.is_object())
                        return it->second;
                }
            }
            return std::move(value);
        }

        template<typename T>
        static T get_config_param (const manapi::json &config, std::string_view name, T value) {
            if (config.is_object()) {
                auto &obb = config.as_object();
                auto it = obb.find(name);
                if (it != obb.end()) {
                    auto res = get_value_config_param<T>(it->second);
                    if (res.has_value())
                        return std::move(res.value());
                }
            }
            return value;
        }
    };
}