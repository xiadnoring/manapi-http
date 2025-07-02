#pragma once

#include "ManapiErrors.hpp"
#include "ManapiUtils.hpp"
#include "ManapiJson.hpp"

namespace manapi {
    class json_mask {
    public:
        json_mask(const std::initializer_list<json> &data);
        json_mask(json data);
        json_mask(const nullptr_t &n = nullptr);
        json_mask(const json_mask &n);
        json_mask(json_mask &&n) noexcept;
        ~json_mask();
        //json_mask &operator=(json_mask &&n) noexcept;

        [[nodiscard]] bool is_enabled () const;
        void set_enabled (bool status);

        [[nodiscard]] manapi::error::status valid (const json &obj) const;
        [[nodiscard]] manapi::error::status valid (const std::map <std::string, std::string> &obj) const;

        [[nodiscard]] const json &get_api_tree () const;
        void set_api_tree (json tree);
        static json OR (json data, bool none = false);
        static json ARRAY (json data, ssize_t min, ssize_t max, bool none = false);
        static json ARRAY (json data, bool none = false);

        void set_complete_status (bool complete);
    private:
        bool enabled;
        bool complete = true;

        json information;
        static void _set_status_prepared (json &data);
        static void _insert_meta_row (json &information, const std::string &key, const json &value);
        static void initial_resolve_information (json &obj);
        [[nodiscard]] manapi::error::status recursive_valid (const json &obj, const json &information, bool is_complex, std::vector<std::string_view> *path) const;
    };
}