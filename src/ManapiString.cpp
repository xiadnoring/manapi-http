#include "ManapiString.hpp"

#include "ManapiDebug.hpp"
#include "ManapiMath.hpp"

/**
 * Fills the string on the right to the specified size of the string
 *
 * @param str   the link to string
 * @param size  the size of the string
 * @param c     the char which will be added at the end of the string
 */
[[maybe_unused]] void manapi::string::rjust (std::string &str, const size_t &size, const char &c) {
    while (str.size() < size)
        str += c;
}

/**
 * Fills the string on the left to the specified size of the string
 *
 * @param str   the link to string
 * @param size  the size of the string
 * @param c     the char which will be added at the start of the string
 */
[[maybe_unused]] void manapi::string::ljust (std::string &str, const size_t &size, const char &c) {
    if (str.size() >= size) return;

    std::string new_string;

    const size_t count = size - str.size();

    while (new_string.size() < count)
        new_string += '0';

    str.insert(0, new_string);
}

constexpr char ptr[] = "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM_-";

std::string manapi::string::random (const size_t &len) {
    return random(len, std::string_view{ptr, sizeof(ptr) - 1});
}

std::string manapi::string::random (const size_t &len, std::string_view src) {
    if (src.empty()) { THROW_MANAPIHTTP_EXCEPTION2(ERR_BUG, "random(...): the 'src' parameter is empty"); }
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

    return std::move(n);
}