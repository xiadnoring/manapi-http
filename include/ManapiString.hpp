#pragma once

#include <string>

namespace manapi::string {
    [[maybe_unused]] void rjust (std::string &str, const size_t &size, const char &c);
    [[maybe_unused]] void ljust (std::string &str, const size_t &size, const char &c);
    std::string random (const size_t &len);
}