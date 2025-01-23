#pragma once

#include <string>

namespace manapi::unicode {
    int count_of_octet (unsigned char c);

    std::string str32to4 (const std::u32string &str32);
    std::string str32to4 (const char32_t &str32);
    std::u32string str4to32 (const std::string &str);

    std::string str16to4 (const std::u16string &str16);
    std::string str16to4 (const char16_t &str16);
    std::u16string str4to16 (const std::string &str);

    bool is_space_symbol (const char &symbol);
    bool is_space_symbol (const unsigned char &symbol);
    bool is_space_symbol (const wchar_t &symbol);
    bool is_space_symbol (const char32_t &symbol);
    char hex2dec (char a);
    char dec2hex (char a);
    std::string escape_string (const std::string &str, const char &quotes = '"');
    std::u32string escape_string (const std::u32string &str);
    bool valid_special_symbol (const char &c);
    bool escape_char_need (const char &c, const char &quotes = '"');
    bool escape_char_need (const wchar_t &c);
    bool escape_char_need (const char32_t &c);
}
