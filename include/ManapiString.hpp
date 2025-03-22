#pragma once

#include <string>
#include "ManapiUtils.hpp"

namespace manapi::string {
    [[maybe_unused]] void rjust (std::string &str, const size_t &size, const char &c);
    [[maybe_unused]] void ljust (std::string &str, const size_t &size, const char &c);
    std::string random (const size_t &len);
    std::string random (const size_t &len, std::string_view src);
    std::vector<std::string_view> split (std::string_view s, char c);
}