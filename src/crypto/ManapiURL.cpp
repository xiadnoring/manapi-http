#include "crypto/ManapiURL.hpp"

#include "ManapiUnicode.hpp"

const std::set<char> manapi::crypto::url_allowed_symbols = {'-', '_', '.', '~', '!', '*', '\'', '(', ')', ';', '/', '?', ':', '@', '&', '=', '+', '$', ',', '.', '#', '[', ']', '%'};

std::string manapi::crypto::encode_url(const std::string &str) {
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

std::string manapi::crypto::decode_url(const std::string &str) {
    std::string ret;

    std::size_t i;

    for (i = 0; i < str.size(); i++){
        if (!crypto::url_allowed_symbol(str[i])) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_PARSE_INVALID_CHAR, "decode_url: invalid char");
        }

        if(str[i] != '%'){
            if(str[i] == '+') {
                ret += ' ';
            }
            else {
                ret += str[i];
            }
        }
        else{
            if (str.size() <= i + 2) {
                ret += str[i];
                continue;
            }
            if (!(isalnum(str[i+1]) && isalnum(str[i+2]))) {
                ret += str[i];
                continue;
            }
            ret.push_back(static_cast<char> (manapi::unicode::hex2dec(str[i+1]) << 4 | manapi::unicode::hex2dec(
                                     str[i+2])));
            i = i + 2;
        }
    }
    return ret;
}

bool manapi::crypto::url_allowed_symbol(const char &c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || url_allowed_symbols.contains(c);
}
