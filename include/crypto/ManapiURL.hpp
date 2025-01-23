#pragma once

#include <string>
#include <fstream>
#include <iomanip>
#include <set>

namespace manapi::crypto {
    static const std::set <char> url_allowed_symbols = {'-', '_', '.', '~', '!', '*', '\'', '(', ')', ';', '/', '?', ':', '@', '&', '=', '+', '$', ',', '.', '#', '[', ']', '%'};

    inline std::string encode_url(const std::string &str) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;

        for (const auto &c: str)
        {
            if (isalnum(c) || c == '_' || c == '-' || c == '.' || c == '~')
            {
                escaped << c;
                continue;
            }

            if (c == ' ')
            {
                escaped << '+';
                continue;
            }

            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned int> (c));
            escaped << std::nouppercase;
        }

        return escaped.str();
    }

    inline std::string decode_url(const std::string &str) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "this function has not been implemented yet.");
        return "";
    }

    inline bool url_allowed_symbol(const char &c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || url_allowed_symbols.contains(c);
    }
}