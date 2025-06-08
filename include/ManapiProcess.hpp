#pragma once

#include <string>

#include "ManapiErrors.hpp"

namespace manapi::process {
    manapi::error::status set_env (std::string_view name, std::string_view key);
    manapi::error::status_or<std::string> get_env (std::string_view name);
}
