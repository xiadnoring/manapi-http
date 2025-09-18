#pragma once

#include <string>
#include <set>

#include "../ManapiErrors.hpp"
#include "../ManapiDebug.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::encoding {
    extern const std::set <char> url_allowed_symbols;

    void encode_url(std::string &dest, std::string_view str);

    std::string encode_url(std::string_view str);

    void decode_url(std::string &dest, std::string_view str);

    std::string decode_url(std::string_view str);

    bool url_allowed_symbol(const char &c);
}