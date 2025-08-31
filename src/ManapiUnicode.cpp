#include <utility>
#include "ext/uv.h"

#include "encoding/ManapiUnicode.hpp"
#include "include/ManapiUtils.hpp"
#include "ManapiDebug.hpp"

#define BIT_AT(n, i, t) ((n >> (sizeof(t) * 8 - (i + 1))) & 1)

static const unsigned char hextable[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,       /* 0x30 - 0x3f */
    0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x40 - 0x4f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* 0x50 - 0x5f */
    0, 10, 11, 12, 13, 14, 15                             /* 0x60 - 0x66 */
};

static const unsigned char hextable2[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
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
    std::size_t size = str.size() + 2;

    // int utf_size = 0;
    // uint8_t utf_tmp = 0;

    for (auto & c : str) {
        switch (c) {
            case '\n':
            case '\r':
            case '\f':
            case '\b':
            case '\t':
            case '\"':
            case '\\':
                size++;
            break;
            default:
                // if (utf_size) {
                //     if ((c & 0xC0)!=0x80)
                //         throw std::runtime_error("utf8 invalid");
                //
                //     utf_size--;
                // }
                // else if (!isprint(c)) {
                //     utf_size = count_of_octet(c);
                //
                //     if(utf_size > 3)
                //         throw std::runtime_error("utf8 invalid");
                //
                //     size += sizeof ("\u0000") - 1 - utf_size;
                //
                //     utf_size--;
                // }

            break;
        }
    }
    std::string s;
    s.reserve(size);
    s.push_back('"');
    for (auto & c : str) {
        switch (c) {
            case '\n':
                s.append("\\n");
            break;
            case '\r':
                s.append("\\r");
            break;
            case '\f':
                s.append("\\f");
            break;
            case '\b':
                s.append("\\b");
            break;
            case '\t':
                s.append("\\t");
            break;
            case '\"':
                s.append("\\\"");
            break;
            case '\\':
                s.append("\\\\");
            break;
            default:
                s.push_back(c);
                // if (utf_size) {
                //     switch (utf_size) {
                //         case 1:
                //             s.push_back(static_cast<char>(onedec2hex(((utf_tmp << 2) | (c & 0x30)))));
                //             s.push_back(static_cast<char>(onedec2hex(c & 0x0F)));
                //
                //             utf_tmp = 0;
                //             break;
                //
                //         case 2:
                //             s.push_back(static_cast<char>(onedec2hex(c & 0x3C)));
                //             utf_tmp = c & 0x3;
                //             break;
                //
                //         default:
                //             break;
                //     }
                //     s.push_back(static_cast<char>(onedec2hex((c >> 4))));
                //
                //     utf_size--;
                // }
                // else if (!isprint(c)) {
                //     utf_size = count_of_octet(c);
                //
                //     s.append("\\u");
                //
                //     switch (utf_size) {
                //         case 1:
                //             s.append("00");
                //             s.push_back(static_cast<char>(onedec2hex((c >> 4))));
                //             s.push_back(static_cast<char>(onedec2hex((c & 0x0F))));
                //             break;
                //         case 2:
                //             s.push_back('0');
                //             s.push_back(static_cast<char>(onedec2hex((c & 0x1C) >> 2)));
                //             utf_tmp = (c & 0x3);
                //             break;
                //         case 3:
                //             s.push_back(static_cast<char>(onedec2hex((c & 0x0F))));
                //             break;
                //
                //         default:
                //             break;
                //     }
                //
                //     utf_size--;
                // }
            break;
        }
    }
    s.push_back('"');

    return std::move(s);
}

uint8_t manapi::unicode::onehex2dec(uint8_t c) MANAPIHTTP_NOEXCEPT {
    return hextable[c - '0'];
}

uint8_t manapi::unicode::onedec2hex(uint8_t c) MANAPIHTTP_NOEXCEPT {
    return hextable2[c];
}

