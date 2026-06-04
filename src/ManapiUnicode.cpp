#include <utility>

#include "encoding/ManapiUnicode.hpp"
#include "include/ManapiUtils.hpp"
#include "ManapiDebug.hpp"
#include "json/ManapiJsonInternal.hpp"

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

uint32_t manapi::unicode::count_of_octet(unsigned char c) {
    uint32_t i = 0;

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
// manapi::status_or<std::string> manapi::unicode::utf::str32to8(std::u32string_view str32) {
//
// }
//
// manapi::status_or<std::string> manapi::unicode::utf::str32to8(char32_t str32) {
//
// }
//
// manapi::status_or<std::u32string> manapi::unicode::utf::str8to32(std::string_view str) {
//     uv_wtf8_to_utf16()
// }
//
// manapi::status_or<std::string> manapi::unicode::utf::str16to8(std::u16string_view str16) {
//
// }
//
// manapi::status_or<std::string> manapi::unicode::utf::str16to8(char16_t str16) {
//
// }
//
// manapi::status_or<std::u16string> manapi::unicode::utf::str8to16(std::string_view str) {
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

void manapi::unicode::escape_string(std::string_view str, char *out) {
    //auto const p = out;
    *out++ = '"';
    for (auto const c : str) {
        switch (c) {
            case '\n':
                *out++ = '\\';
                *out++ = 'n';
            break;
            case '\r':
                *out++ = '\\';
                *out++ = 'r';
            break;
            case '\f':
                *out++ = '\\';
                *out++ = 'f';
            break;
            case '\b':
                *out++ = '\\';
                *out++ = 'b';
            break;
            case '\t':
                *out++ = '\\';
                *out++ = 't';
            break;
            case '\"':
                *out++ = '\\';
                *out++ = '\"';
            break;
            case '\\':
                *out++ = '\\';
                *out++ = '\\';
            break;
            default:
                *out++ = c;
            break;
        }
    }
    *out++ = '"';
}

void manapi::unicode::escape_string(std::string_view str, manapi::json_dump_buffer *out) {
    out->push_back('"');
    for (auto const c : str) {
        switch (c) {
            case '\n':
                out->push_back('\\');
                out->push_back('n');
                break;
            case '\r':
                out->push_back('\\');
                out->push_back('r');
                break;
            case '\f':
                out->push_back('\\');
                out->push_back('f');
                break;
            case '\b':
                out->push_back('\\');
                out->push_back('b');
                break;
            case '\t':
                out->push_back('\\');
                out->push_back('t');
                break;
            case '\"':
                out->push_back('\\');
                out->push_back('\"');
                break;
            case '\\':
                out->push_back('\\');
                out->push_back('\\');
                break;
            default:
                out->push_back(c);
            break;
        }
    }
    out->push_back('"');
}

std::size_t manapi::unicode::escape_string_size(std::string_view str) {
    std::size_t size = str.size() + 2;

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
                break;
        }
    }

    return size;
}

// char manapi::unicode::hex2dec(char a) {
//     a = static_cast<char> (std::toupper(a));
//     return static_cast <char>(a >= 'A' ? a - 'A' + 10 : a - '0');
// }

std::string manapi::unicode::escape_string (std::string_view str) {
    std::string s;
    s.resize(unicode::escape_string_size(str));
    unicode::escape_string(str, s.data());
    return std::move(s);
}

uint8_t manapi::unicode::onehex2dec(uint8_t c) MANAPIHTTP_NOEXCEPT {
    return hextable[c - '0'];
}

uint8_t manapi::unicode::onedec2hex(uint8_t c) MANAPIHTTP_NOEXCEPT {
    return hextable2[c];
}

