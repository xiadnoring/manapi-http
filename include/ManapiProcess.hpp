#pragma once

#include <string>

namespace manapi::process {
    void set_env (std::string_view name, std::string_view key);
    std::string get_env (std::string_view name);
}