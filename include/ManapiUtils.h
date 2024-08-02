#ifndef MANAPIHTTP_MANAPIUTILS_H
#define MANAPIHTTP_MANAPIUTILS_H

#include <string>
#include <map>
#include <vector>
#include <iostream>

namespace manapi::net {
    struct header_value_t {
        std::string                             value;
        std::map <std::string, std::string>     params;
    };

    typedef std::map <std::string, std::vector <manapi::net::header_value_t> > headers_t;
}

namespace manapi::utils {

    struct replace_founded_item {
        std::string key;
        const std::string *value;
        std::pair <ssize_t, ssize_t> pos;
    };

#define MANAPI_LOG(msg, ...)                manapi::utils::_log (__LINE__, __FILE_NAME__, __FUNCTION__, false, msg, __VA_ARGS__)
#define THROW_MANAPI_EXCEPTION(msg, ...)    manapi::utils::_log (__LINE__, __FILE_NAME__, __FUNCTION__, true, msg, __VA_ARGS__);

    typedef std::map <std::string, std::string> MAP_STR_STR;
    typedef std::vector <std::string>           VEC_STR;

    bool is_space_symbol    (const char &symbol);
    bool is_space_symbol    (const wchar_t &symbol);
    bool is_space_symbol    (const char32_t &symbol);

    size_t pow (const size_t &x, const size_t &count);

    [[maybe_unused]] void rjust (std::string &str, const size_t &size, const char &c);
    [[maybe_unused]] void ljust (std::string &str, const size_t &size, const char &c);

    char            hex2dec             (const char &a);
    std::string     escape_string       (const std::string &str);
    std::u32string  escape_string       (const std::u32string &str);
    bool            valid_special_symbol(const char &c);
    bool            escape_char_need    (const char &c);
    bool            escape_char_need    (const wchar_t &c);
    bool            escape_char_need    (const char32_t &c);

    std::string generate_cache_name (const std::string &basename, const std::string &ext);

    // random

    size_t      random              (const size_t &_min, const size_t &_max);
    std::string random_string       (const size_t &len);

    std::string time                (const std::string &fmt, bool local = false);

    std::vector <replace_founded_item> found_replacers_in_file (const std::string &path, const size_t &start, const size_t &size, const MAP_STR_STR &replacers);

    // ====================[ Headers ]============================

    std::vector <manapi::net::header_value_t>   parse_header_value (const std::string &header_value);
    std::pair<std::string, std::string>         parse_header (const std::string &header);
    std::string                                 stringify_header (const std::pair<std::string, std::string> &header);
    std::string                                 stringify_header_value (const std::vector <manapi::net::header_value_t> &header_value);

    // ====================[ Classes ]============================

    class exception : public std::exception {
    public:
        explicit exception (std::string message_);
        [[nodiscard]] const char * what() const noexcept override;
    private:
        std::string message;
    };

    // ====================[ Strings ]===============================

    std::string     str32to4    (const std::u32string &str32);
    std::string     str32to4    (const char32_t &str32);
    std::u32string  str4to32    (const std::string &str);

    std::string     str16to4    (const std::u16string &str16);
    std::string     str16to4    (const char16_t &str16);
    std::u16string  str4to16    (const std::string &str);

    // ====================[ Debug ]===============================

    template <class... Args>
    void _log               (const size_t &line, const std::string &file_name, const std::string &func, const bool &except, const std::string &format, Args&& ...args)
    {
        std::string head = std::format ("[{}]: {}() ({}:{}): ", utils::time("%H:%M:%S", true), func, file_name, line);
        std::string information = std::vformat(format, std::make_format_args(args...));

        if (except)
        {
            std::cerr << head << information << "\n";
        }
        else
        {
            std::cout << head << information << "\n";
        }

        if (except)
        {
            throw manapi::utils::exception (information);
        }
    }
}

namespace manapi::net {
    struct request_data_t {
        // size of the part of the headers in the buffer (READ) [HHHH]BBBBBBB <- 4
        size_t                              headers_part;
        size_t                              headers_size;
        // just headers
        std::map<std::string, std::string>  headers;
        // contains params from url .../[param1]-[param2]/...
        std::map<std::string, std::string>  params;

        // GET, POST, HEAD
        std::string                         method;
        // PATH
        std::string                         uri;
        // version http
        std::string                         http;
        // split by '/'
        std::vector <std::string>           path;
        // index of the element where URL get params in the path
        ssize_t                             divided;

        char*                               body_ptr;
        size_t                              body_index;
        size_t                              body_left;
        size_t                              body_size;
        // size of the part of the body in the buffer (READ) HHHH[BBBBB] <- 5
        size_t                              body_part;

        bool                                has_body    = false;
    };
}

#endif //MANAPIHTTP_MANAPIUTILS_H
