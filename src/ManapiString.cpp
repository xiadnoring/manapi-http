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

void manapi::string::random(char *dst, size_t len) MANAPIHTTP_NOEXCEPT {
    random(dst, len, std::string_view{ptr, sizeof(ptr) - 1});
}

std::string manapi::string::random (size_t len, std::string_view src) {
    if (src.empty()) {
        throw manapi::exception(ERR_INTERNAL, "random(...): 'src' parameter is empty");
    }

    const size_t back = src.size() - 1;

    std::string result;
    result.resize(len);

    for (size_t i = 0; i < len; i++) {
        result[i] = src[manapi::math::random(0, back)];
    }

    return std::move(result);
}

void manapi::string::random(char *dst, size_t len, std::string_view src) MANAPIHTTP_NOEXCEPT {
    if (src.empty()) {
        manapi_log_error("random:source is empty");
        return;
    }

    const size_t back = src.size() - 1;

    for (size_t i = 0; i < len; i++) {
        dst[i] = src[manapi::math::random(0, back)];
    }
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
    std::size_t i = 0;
    std::size_t const size = lhs.size();

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

std::string manapi::string::fill(size_t s, char c) {
    std::string b;
    b.resize(s);
    memset(b.data(), c, s);
    return std::move(b);
}

std::size_t manapi::string::count(char c, std::string_view str) MANAPIHTTP_NOEXCEPT {
    std::size_t res = 0;
    auto it = str.find(c);
    while (it != std::string_view::npos) {
        res++;
        str = str.substr(it + 1);
        it = str.find(c);
    }
    return res;
}

std::size_t manapi::string::replace(std::string &s, std::string_view from, std::string_view to, ssize_t cnt) {
    std::size_t res = 0;
    std::size_t shift;
    bool flg;

    if (to.size() > from.size()) {
        flg = false;
        shift = to.size() - from.size();
    }
    else {
        flg = true;
        shift = from.size() - to.size();
    }

    while (cnt != 0) {
        auto it = s.find(from);
        if (it == std::string::npos) {
            break;
        }

        if (flg) {
            memmove(s.data() + it + to.size(), s.data() + it + from.size(), s.size() - it - from.size());
            memcpy(s.data() + it, to.data(), to.size());

            s.resize(s.size() - shift);
        }
        else {
            s.resize(s.size() + shift);

            memmove(s.data() + it + shift, s.data() + it, s.size() - it - shift);
            memcpy(s.data() + it, to.data(), to.size());
        }

        res++;
        cnt--;
    }
    return res;
}
