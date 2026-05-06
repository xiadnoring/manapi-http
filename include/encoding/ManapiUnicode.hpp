#pragma once

#include <string>

#include "../ManapiUtils.hpp"
#include "../ManapiErrors.hpp"

namespace manapi::unicode {
    /**
     * get a count of the provided octets
     * @param c octets
     * @return number of octets
     */
    uint32_t count_of_octet (unsigned char c);

    /**
     * Get information about the provided char
     * @param symbol symbol char to check
     * @return true if the char is a whitespace character ('\r', '\n', ' ', '\t')
     */
    bool is_space_symbol (char symbol);

    /**
     * Get information about the provided char
     * @param symbol symbol char to check
     * @return true if the char is a whitespace character ('\r', '\n', ' ', '\t')
     */
    bool is_space_symbol (unsigned char symbol);

    /**
     * Get information about the provided char
     * @param symbol symbol char to check
     * @return true if the char is a whitespace character ('\r', '\n', ' ', '\t')
     */
    bool is_space_symbol (wchar_t symbol);

    /**
     * Get information about the provided char
     * @param symbol symbol char to check
     * @return true if the char is a whitespace character ('\r', '\n', ' ', '\t')
     */
    bool is_space_symbol (char32_t symbol);

    /**
     * Escape string. It works like as std::quoted() function
     * @param str source string
     * @param quotes delimiter
     * @return escaped string
     */
    std::string escape_string (std::string_view str, char quotes = '"');

    uint8_t onehex2dec (uint8_t c) MANAPIHTTP_NOEXCEPT;

    uint8_t onedec2hex (uint8_t c) MANAPIHTTP_NOEXCEPT;
}
