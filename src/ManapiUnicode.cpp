#include "ManapiUnicode.hpp"

#include <utility>
#if _WIN32
#   include <codecvt>
#else
#   include <unicode/utf32.h>
#   include <unicode/utf16.h>
#   include <unicode/utf8.h>
#endif
#include <codecvt>
#include <locale>

#define BIT_AT(n, i, t) ((n >> (sizeof(t) * 8 - (i + 1))) & 1)

int manapi::unicode::count_of_octet(unsigned char c) {
    int i = 0;

    if (c < 128) {
        return 1;
    }

    for (; i < 8; i++) {
        if (BIT_AT(c, i, char) == 0) {
            return i;
        }
    }

    return i;
}

std::string manapi::unicode::str16to4 (const std::u16string &str16)
{
    return std::move(std::wstring_convert< std::codecvt_utf8<char16_t>, char16_t >{}.to_bytes(str16));
}
std::string manapi::unicode::str16to4 (const char16_t &str16)
{
    return std::move(std::wstring_convert< std::codecvt_utf8<char16_t>, char16_t >{}.to_bytes(str16));
}
std::u16string manapi::unicode::str4to16 (const std::string &str)
{
    return std::move(std::wstring_convert< std::codecvt_utf8<char16_t>, char16_t >{}.from_bytes(str));
}
std::string manapi::unicode::str32to4 (const std::u32string &str32)
{
    return std::move(std::wstring_convert< std::codecvt_utf8<char32_t>, char32_t >{}.to_bytes(str32));
}
std::string manapi::unicode::str32to4 (const char32_t &str32)
{
    return std::move(std::wstring_convert< std::codecvt_utf8<char32_t>, char32_t >{}.to_bytes(str32));
}
std::u32string manapi::unicode::str4to32 (const std::string &str)
{
    return std::move(std::wstring_convert< std::codecvt_utf8<char32_t>, char32_t >{}.from_bytes(str));
}

bool manapi::unicode::is_space_symbol (const char &symbol) {
    return symbol == '\r' || symbol == '\n' || symbol == '\t' || symbol == ' ';
}

bool manapi::unicode::is_space_symbol(const unsigned char &symbol) {
    return is_space_symbol(static_cast<char> (symbol));
}

bool manapi::unicode::is_space_symbol (const wchar_t &symbol) {
    return symbol == '\r' || symbol == '\n' || symbol == '\t' || symbol == ' ';
}

bool manapi::unicode::is_space_symbol (const char32_t &symbol) {
    return symbol == '\r' || symbol == '\n' || symbol == '\t' || symbol == ' ';
}

char manapi::unicode::hex2dec(char a) {
    a = static_cast<char> (std::toupper(a));
    return static_cast <char>(a >= 'A' ? a - 'A' + 10 : a - '0');
}

char manapi::unicode::dec2hex(char a) {
    return static_cast <char> (a >= 10 ? a - 10 + 'A' : a + '0');
}

std::string manapi::unicode::escape_string (const std::string &str, const char &quotes) {
    std::string escaped;

    for (const auto &i : str) {
        switch (i) {
            case '\n':
                escaped += "\\n";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\b':
                escaped += "\\b";
                break;
            default:
                if (escape_char_need(i, quotes)) {
                    escaped.push_back('\\');
                    escaped.push_back(i);

                    continue;
                }

                escaped += i;
        }
    }

    return std::move(escaped);
}

std::u32string manapi::unicode::escape_string (const std::u32string &str) {
    std::u32string escaped;

    for (const auto &i : str) {
        char32_t a;
        bool is_special_char = true;

        switch (i) {
            case '\n':
                a = 'n';
                break;
            case '\r':
                a = 'r';
                break;
            case '\t':
                a = 't';
                break;
            case '\b':
                a = 'b';
                break;
            case '\f':
                a = 'f';
                break;
            default:
                is_special_char = false;

                if (escape_char_need(i)) {
                    escaped.push_back('\\');
                    escaped.push_back(i);

                    continue;
                }

                escaped += i;
        }
        if (is_special_char)
        {
            escaped.push_back('\\');
            escaped.push_back(a);
        }
    }

    return std::move(escaped);
}

bool manapi::unicode::escape_char_need (const char &ch, const char &quotes) {
    // if ch >= 128 -> non-ascii (maybe utf)
    return ch < 127 && ( ch == quotes || ch == '\\' || ch == '/');
}

bool manapi::unicode::escape_char_need (const wchar_t &ch) {
    return ch == '"' || ch == '\\' || ch == '/';
}

bool manapi::unicode::escape_char_need (const char32_t &ch) {
    return ch == '"' || ch == '\\' || ch == '/';
}

bool manapi::unicode::valid_special_symbol(const char &c) {
    /**
     * ascii
     * 0 - 127
     */
    return c >= 0;
}