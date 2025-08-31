#include "encoding/ManapiURL.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "../include/ManapiUtils.hpp"

const std::set<char> manapi::encoding::url_allowed_symbols = {'-', '_', '.', '~', '!', '*', '\'', '(', ')', ';', '/', '?', ':', '@', '&', '=', '+', '$', ',', '.', '#', '[', ']', '%'};

void manapi::encoding::encode_url(std::string &dest, std::string_view str) {
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

    dest += escaped.str();
}

std::string manapi::encoding::encode_url(std::string_view str) {
    std::string dest;
    encode_url (dest, str);
    return std::move(dest);
}

void manapi::encoding::decode_url(std::string &dest, std::string_view str) {
    std::size_t i;

    for (i = 0; i < str.size(); i++){
        if (!encoding::url_allowed_symbol(str[i])) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_INVALID_ARGUMENT, "decode_url: invalid char");
        }

        if(str[i] != '%'){
            if(str[i] == '+') {
                dest += ' ';
            }
            else {
                dest += str[i];
            }
        }
        else{
            if (str.size() <= i + 2) {
                dest += str[i];
                continue;
            }
            if (!(isalnum(str[i+1]) && isalnum(str[i+2]))) {
                dest += str[i];
                continue;
            }
            dest.push_back(static_cast<char> (manapi::unicode::onehex2dec(str[i+1]) << 4 | manapi::unicode::onehex2dec(
                                     str[i+2])));
            i = i + 2;
        }
    }
}

std::string manapi::encoding::decode_url(std::string_view str) {
    std::string dest;
    decode_url(dest, str);
    return std::move(dest);
}

bool manapi::encoding::url_allowed_symbol(const char &c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || url_allowed_symbols.contains(c);
}
