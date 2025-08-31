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
    int count_of_octet (unsigned char c);

    // namespace utf {
    //     /**
    //      * convert utf32 string to utf8 string
    //      * @param str32 utf32 source string
    //      * @return utf8 string, othewise it returns InternalError, ResourceExhausted
    //      */
    //     manapi::error::status_or<std::string> str32to8 (std::u32string_view str32);
    //
    //     /**
    //      * convert utf32 char to utf8 string
    //      * @param str32 utf32 source char
    //      * @return utf8 string, othewise it returns InternalError, ResourceExhausted
    //      */
    //     manapi::error::status_or<std::string> str32to8 (char32_t str32);
    //
    //     /**
    //      * convert utf8 string to utf32 string
    //      * @param str utf8 source string
    //      * @return utf32 string, othewise it returns InternalError, ResourceExhausted
    //      */
    //     manapi::error::status_or<std::u32string> str8to32 (std::string_view str);
    //
    //     /**
    //      * convert utf16 string to utf8 string
    //      * @param str16 utf16 source string
    //      * @return utf8 string, othewise it returns InternalError, ResourceExhausted
    //      */
    //     manapi::error::status_or<std::string> str16to8 (std::u16string_view str16);
    //
    //     /**
    //      * convert utf16 char to utf8 string
    //      * @param str16 utf16 source char
    //      * @return utf8 string, othewise it returns InternalError, ResourceExhausted
    //      */
    //     manapi::error::status_or<std::string>  str16to8 (char16_t str16);
    //
    //     /**
    //      * convert utf8 string to utf16 string
    //      * @param str utf8 source string
    //      * @return utf8 string, othewise it returns InternalError, ResourceExhausted
    //      */
    //     manapi::error::status_or<std::u16string> str8to16 (std::string_view str);
    // }

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
