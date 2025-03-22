#pragma once

#include <map>
#include <string>

namespace manapi::net::http {
    std::map<std::string, std::string> parse_get_params (std::string_view params);
}
