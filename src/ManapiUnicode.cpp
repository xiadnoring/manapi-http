#include "encoding/ManapiUnicode.hpp"
#include "include/ManapiUtils.hpp"
#include <utility>
#include <uv.h>

#include "ManapiDebug.hpp"

#define BIT_AT(n, i, t) ((n >> (sizeof(t) * 8 - (i + 1))) & 1)

static const unsigned char hextable[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,       /* 0x30 - 0x3f */
    0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x40 - 0x4f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* 0x50 - 0x5f */
    0, 10, 11, 12, 13, 14, 15                             /* 0x60 - 0x66 */
};

int manapi::unicode::count_of_octet(unsigned char c) {
    int i = 0;

    if (c < 128)
        return 1;

    for (; i < 8; i++) {
        if (BIT_AT(c, i, char) == 0) {
            return i;
        }
    }

    return i;
}
//
// manapi::error::status_or<std::string> manapi::unicode::utf::str32to8(std::u32string_view str32) {
//
// }
//
// manapi::error::status_or<std::string> manapi::unicode::utf::str32to8(char32_t str32) {
//
// }
//
// manapi::error::status_or<std::u32string> manapi::unicode::utf::str8to32(std::string_view str) {
//     uv_wtf8_to_utf16()
// }
//
// manapi::error::status_or<std::string> manapi::unicode::utf::str16to8(std::u16string_view str16) {
//
// }
//
// manapi::error::status_or<std::string> manapi::unicode::utf::str16to8(char16_t str16) {
//
// }
//
// manapi::error::status_or<std::u16string> manapi::unicode::utf::str8to16(std::string_view str) {
//
// }


bool manapi::unicode::is_space_symbol (char symbol) {
    return symbol == '\r' || symbol == '\n' || symbol == '\t' || symbol == ' ';
}

bool manapi::unicode::is_space_symbol(unsigned char symbol) {
    return is_space_symbol(static_cast<char> (symbol));
}

bool manapi::unicode::is_space_symbol (wchar_t symbol) {
    return symbol == '\r' || symbol == '\n' || symbol == '\t' || symbol == ' ';
}

bool manapi::unicode::is_space_symbol (char32_t symbol) {
    return symbol == '\r' || symbol == '\n' || symbol == '\t' || symbol == ' ';
}

// char manapi::unicode::hex2dec(char a) {
//     a = static_cast<char> (std::toupper(a));
//     return static_cast <char>(a >= 'A' ? a - 'A' + 10 : a - '0');
// }

std::string manapi::unicode::escape_string (std::string_view str, char quotes) {
    std::stringstream s;
    s << std::quoted(str, quotes);
    return s.str();
}

uint8_t manapi::unicode::onehex2dec(uint8_t c) {
    return hextable[c - '0'];
}

