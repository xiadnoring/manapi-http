#include <utility>
#include <array>

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

static constexpr unsigned char hextable2[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

static constexpr std::array<uint8_t, 256> escape_extra_bytes = [] {
    std::array<uint8_t, 256> tbl{};

    tbl['\n'] = 1;
    tbl['\r'] = 1;
    tbl['\f'] = 1;
    tbl['\b'] = 1;
    tbl['\t'] = 1;
    tbl['\"'] = 1;
    tbl['\\'] = 1;

    for (int i = 0; i < 0x20; ++i) {
        if (tbl[i] == 0) {
            tbl[i] = 5;
        }
    }

    return tbl;
}();

struct escape_seq {
    char str[6];
    uint8_t len;
};
static constexpr std::array<escape_seq, 256> escape_table = [] {
    std::array<::escape_seq, 256> tbl{};

    tbl['\n'] = {{'\\', 'n'}, 2};
    tbl['\r'] = {{'\\', 'r'}, 2};
    tbl['\f'] = {{'\\', 'f'}, 2};
    tbl['\b'] = {{'\\', 'b'}, 2};
    tbl['\t'] = {{'\\', 't'}, 2};
    tbl['\"'] = {{'\\', '"'}, 2};
    tbl['\\'] = {{'\\', '\\'}, 2};

    for (int i = 0; i < 0x20; ++i) {
        if (tbl[i].len == 0) {
            tbl[i].str[0] = '\\';
            tbl[i].str[1] = 'u';
            tbl[i].str[2] = '0';
            tbl[i].str[3] = '0';
            tbl[i].str[4] = static_cast<char>(hextable2[i >> 4]);
            tbl[i].str[5] = static_cast<char>(hextable2[i & 0x0F]);
            tbl[i].len = 6;
        }
    }

    return tbl;
}();

static constexpr std::array<bool, 256> needs_escape = [] {
    std::array<bool, 256> tbl{};
    for (int i = 0; i < 256; ++i) {
        tbl[i] = (escape_table[i].len > 0);
    }
    return tbl;
}();

class unicode_buffer : public manapi::json_dump_buffer {
public:
    unicode_buffer (char *str) : m_str(str) {}

    void set (char *str) {
        this->m_str = str;
    }

    void push_back(const char *buffer, std::size_t sz) override {
        ::memcpy(this->m_str, buffer, sz);
        this->m_str += sz;
    }

    void push_back(char c) override {
        *this->m_str = c;
        this->m_str++;
    }
private:
    char *m_str;
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
    unicode_buffer z (out);
    unicode::escape_string (str, &z);
}

void manapi::unicode::escape_string(std::string_view str, manapi::json_dump_buffer *out) {
    out->push_back('"');

    const char* data = str.data();
    const size_t len = str.size();
    size_t i = 0;

    while (i < len) {
        size_t start = i;
        while (i < len && !needs_escape[static_cast<unsigned char>(data[i])]) {
            ++i;
        }

        if (i > start) {
            out->push_back(data + start, i - start);
        }

        if (i < len) {
            auto& esc = escape_table[static_cast<unsigned char>(data[i])];
            out->push_back(esc.str, esc.len);
            ++i;
        }
    }
    out->push_back('"');
}

void manapi::unicode::escape_string(const slice_base *str, manapi::json_dump_buffer *out) {
    out->push_back('"');

    for (const auto b : *str) {
        escape_string(b, out);
    }

    out->push_back('"');
}

std::size_t manapi::unicode::escape_string_size(std::string_view str) {
    std::size_t size = str.size() + 2;
    const auto* data = str.data();
    const auto len = str.size();

    for (std::size_t i = 0; i < len; ++i) {
        size += escape_extra_bytes[static_cast<unsigned char>(data[i])];
    }

    return size;
}

std::size_t manapi::unicode::escape_string_size(const manapi::slice_base *sv) {
    std::size_t size = sv->size() + 2;

    for (const auto b : *sv) {
        const auto* data = b.data();
        const auto len = b.size();
        for (std::size_t i = 0; i < len; ++i) {
            size += escape_extra_bytes[static_cast<unsigned char>(data[i])];
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

