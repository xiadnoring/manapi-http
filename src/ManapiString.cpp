#include <cctype>       // std::tolower
#include <algorithm>    // std::equal
#include <string_view>  // std::string_view
#include <string>       // std::string

#include "ManapiString.hpp"
#include "ManapiDebug.hpp"
#include "ManapiMath.hpp"
#include "./include/ManapiUtils.hpp"

bool insensitive_char_equals(char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) ==
           std::tolower(static_cast<unsigned char>(b));
}

[[maybe_unused]] void manapi::string::rjust (std::string &str, size_t size, char c) {
    while (str.size() < size)
        str += c;
}

[[maybe_unused]] void manapi::string::ljust (std::string &str, size_t size, char c) {
    if (str.size() >= size) return;

    std::string new_string;

    const size_t count = size - str.size();

    while (new_string.size() < count)
        new_string += '0';

    str.insert(0, new_string);
}

constexpr char ptr[] = "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM_-";

std::string manapi::string::random (size_t len) {
    return random(len, std::string_view{ptr, sizeof(ptr) - 1});
}

std::string manapi::string::random (size_t len, std::string_view src) {
    if (src.empty()) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_INTERNAL, "random(...): the 'src' parameter is empty");
    }
    const size_t back = src.size() - 1;

    std::string result;
    result.resize(len);

    for (size_t i = 0; i < len; i++) {
        result[i] = src[manapi::math::random(0, back)];
    }

    return std::move(result);
}

std::vector<std::string_view> manapi::string::split(std::string_view s, char c) {
    std::vector<std::string_view> n;

    std::size_t j = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == c) {
            n.emplace_back(s.data() + j, s.data() + i);
            j = i + 1;
        }
    }

    if (s.size())
        n.emplace_back(s.data() + j, s.data() + s.size());

    return std::move(n);
}

bool manapi::string::equals(std::string_view lhs, std::string_view rhs, int flags) {
    bool res = true;
    ssize_t i = 0;
    ssize_t const size = lhs.size();

    if (lhs.size() != rhs.size()) {
        return false;
    }

    if (flags & 0b11) {
        for (; i < size; i++) {
            if (std::tolower(lhs[i]) != std::tolower(rhs[i])) {
                res = false;
                break;
            }
        }
    }
    else if (flags & 0b01) {
        for (; i < size; i++) {
            if (lhs[i] != std::tolower(rhs[i])) {
                res = false;
                break;
            }
        }
    }
    else if (flags & 0b10) {
        for (; i < size; i++) {
            if (std::tolower(lhs[i]) != rhs[i]) {
                res = false;
                break;
            }
        }
    }
    else {
        for (; i < size; i++) {
            if (lhs[i] != rhs[i]) {
                res = false;
                break;
            }
        }
    }

    return res;
}

void manapi::string::lower_ascii(std::string &n) {
    for (auto &c : n) {
        if (c >= 'A' && c <= 'Z')
            c = c - ('Z' - 'z');
    }
}
