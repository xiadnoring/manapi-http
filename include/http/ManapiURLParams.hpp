#pragma once

#include <map>
#include <string>
#include "../ManapiUtils.hpp"

namespace manapi::net::http {
    std::map<std::string, std::string> parse_get_params (std::string_view params);
}
