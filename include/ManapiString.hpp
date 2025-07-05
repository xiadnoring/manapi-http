#pragma once

#include <string>
#include <vector>
#include "ManapiUtils.hpp"

namespace manapi::string {
    /**
     * Fills the string on the right to the specified size of the string
     *
     * @param str   the link to string
     * @param size  the size of the string
     * @param c     the char which will be added at the end of the string
     */
    [[maybe_unused]] void rjust (std::string &str, size_t size, char c);

    /**
     * Fills the string on the left to the specified size of the string
     *
     * @param str   the link to string
     * @param size  the size of the string
     * @param c     the char which will be added at the start of the string
     */
    [[maybe_unused]] void ljust (std::string &str, size_t size, char c);

    std::string random (size_t len);

    std::string random (size_t len, std::string_view src);

    std::vector<std::string_view> split (std::string_view s, char c);

    /**
     * Equals two strings and return a result
     *
     * @param lhs Original string
     * @param rhs Other string
     * @param flags @code The first byte@endcode means insansitive
     * in the second string, @code the second byte@endcode means
     * insansitive in the first string
     * @return
     */
    bool equals(std::string_view lhs, std::string_view rhs, int flags = 0);

    void lower_ascii (std::string &n);
}