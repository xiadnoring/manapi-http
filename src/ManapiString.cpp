#include "ManapiString.hpp"

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

std::string manapi::string::random (const size_t &len) {
    const char ptr[] = "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM_-";
    const size_t back = sizeof(ptr) - 2;

    std::string result;
    result.resize(len);

    for (size_t i = 0; i < len; i++) {
        result[i] = ptr[manapi::math::random(0, back)];
    }

    return std::move(result);
}