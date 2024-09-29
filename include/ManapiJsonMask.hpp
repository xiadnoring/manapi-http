#ifndef MANAPIHTTP_MANAPIJSONMASK_H
#define MANAPIHTTP_MANAPIJSONMASK_H

#include "ManapiJson.hpp"

namespace manapi::net::utils {
    class json_mask {
    public:
        json_mask(const std::initializer_list<json> &data);
        json_mask(const nullptr_t &n);
        ~json_mask();

        [[nodiscard]] bool is_enabled () const;
        void set_enabled (const bool &status);

        [[nodiscard]] bool valid (const json &obj) const;
        [[nodiscard]] bool valid (const std::map <std::string, std::string> &obj) const;
    private:
        json information;
        bool enabled;

        static void initial_resolve_information (json &obj);
        static bool recursive_valid (const json &obj, const json &information, const bool &is_complex = true);
        static bool default_compare_information (const json &obj, const json &information, const bool &by_size = true);
    };
}

#endif //MANAPIHTTP_MANAPIJSONMASK_H