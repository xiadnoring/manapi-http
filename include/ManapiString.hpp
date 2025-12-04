/**
 * @file ManapiString.hpp
 * @brief String Utilities
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <string>
#include <vector>

#include "./ManapiUtils.hpp"

/**
 * Namespace with string utilities
 */
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

    /**
     * Get randomly generated string
     *
     * @param len the size of the output string
     * @return randomly generated string
     */
    std::string random (size_t len);
    /**
     * Get randomly generated string
     *
     * @param dst result
     * @param len the size of the output string
     * @return randomly generated string
     */
    void random (char *dst, size_t len) MANAPIHTTP_NOEXCEPT;

    /**
     * Get randomly generated string from the 'src' chars
     *
     * @param len the size of the output string
     * @param src chars which will be used to generate new string
     * @return randomly generated string
     */
    std::string random (size_t len, std::string_view src);

    /**
     * Get randomly generated string from the 'src' chars
     *
     * @param dst result
     * @param len the size of the output string
     * @param src chars which will be used to generate new string
     * @return randomly generated string
     */
    void random (char *dst, size_t len, std::string_view src) MANAPIHTTP_NOEXCEPT;

    /**
     * Split the string by the separator
     *
     * @param s the source string
     * @param c the separator
     * @return array of parts of the source string
     */
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

    /**
     * Make lower all chars in the source string
     *
     * @param n the source string
     */
    void lower_ascii (std::string &n);

    /**
     * Generates a string of size |s| and fills in it with |c|
     * @param s
     * @param c
     * @return
     */
    std::string fill (size_t s, char c);

    /**
     * Counts number of |c| in |str|
     * @param c char
     * @param str source string
     * @return
     */
    std::size_t count (char c, std::string_view str) MANAPIHTTP_NOEXCEPT;

    /**
     * Replaces |from| to |to| in |s| and returns the number of substitutions
     * @param s source string
     * @param from substring
     * @param to substring
     * @param cnt maximum number of substitutions. -1 is infinity
     * @return number of substitutions
     */
    ssize_t replace (std::string &s, std::string_view from, std::string_view to, ssize_t cnt = -1);
}