#pragma once

#include <string>
#include <fstream>
#include <iomanip>
#include <set>

#include "../ManapiErrors.hpp"
#include "../ManapiDebug.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::crypto {
    extern const std::set <char> url_allowed_symbols;

    std::string encode_url(const std::string &str);

    std::string decode_url(const std::string &str);

    bool url_allowed_symbol(const char &c);
}