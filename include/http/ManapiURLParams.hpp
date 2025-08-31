#pragma once

#include <map>
#include <string>

#include "../ManapiUtils.hpp"

namespace manapi::net::http {
    DLLExportImport std::map<std::string, std::string, std::less<>> parse_get_params (std::string_view params);
}
