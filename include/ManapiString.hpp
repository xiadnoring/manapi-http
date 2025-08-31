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
    DLLExportImport [[maybe_unused]] void rjust (std::string &str, size_t size, char c);

    /**
     * Fills the string on the left to the specified size of the string
     *
     * @param str   the link to string
     * @param size  the size of the string
     * @param c     the char which will be added at the start of the string
     */
    DLLExportImport [[maybe_unused]] void ljust (std::string &str, size_t size, char c);

    /**
     * Get randomly generated string
     *
     * @param len the size of the output string
     * @return randomly generated string
     */
    DLLExportImport std::string random (size_t len);
    /**
     * Get randomly generated string
     *
     * @param dst result
     * @param len the size of the output string
     * @return randomly generated string
     */
    DLLExportImport void random (char *dst, size_t len) MANAPIHTTP_NOEXCEPT;

    /**
     * Get randomly generated string from the 'src' chars
     *
     * @param len the size of the output string
     * @param src chars which will be used to generate new string
     * @return randomly generated string
     */
    DLLExportImport std::string random (size_t len, std::string_view src);

    /**
     * Get randomly generated string from the 'src' chars
     *
     * @param dst result
     * @param len the size of the output string
     * @param src chars which will be used to generate new string
     * @return randomly generated string
     */
    DLLExportImport void random (char *dst, size_t len, std::string_view src) MANAPIHTTP_NOEXCEPT;

    /**
     * Split the string by the separator
     *
     * @param s the source string
     * @param c the separator
     * @return array of parts of the source string
     */
    DLLExportImport std::vector<std::string_view> split (std::string_view s, char c);

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
    DLLExportImport bool equals(std::string_view lhs, std::string_view rhs, int flags = 0);

    /**
     * Make lower all chars in the source string
     *
     * @param n the source string
     */
    DLLExportImport void lower_ascii (std::string &n);

    /**
     * Generates a string of size |s| and fills in it with |c|
     * @param s
     * @param c
     * @return
     */
    DLLExportImport std::string fill (size_t s, char c);
}