#ifndef MANAPIHTTP_MANAPIUTILS_H
#define MANAPIHTTP_MANAPIUTILS_H

#include <string>
#include <map>
#include <vector>

namespace manapi::utils {

    struct replace_founded_item {
        std::string key;
        const std::string *value;
        std::pair <ssize_t, ssize_t> pos;
    };

#define MANAPI_LOG(msg, ...)    manapi::utils::_log (__LINE__, __FILE_NAME__, msg, __VA_ARGS__)
#define MANAPI_LOG_ERROR(e, msg, ...) manapi::utils::_log_error(__LINE__, __FILE_NAME__, e, msg, __VA_ARGS__)

    typedef std::map <std::string, std::string> MAP_STR_STR;
    typedef std::vector <std::string>           VEC_STR;

    void _log               (const size_t &line, const char *file_name, const char *format, ...);
    void _log_error         (const size_t &line, const char *file_name, const std::exception &e, const char *format,...);
    bool is_space_symbol    (const char &symbol);

    size_t pow (const size_t &x, const size_t &count);

    [[maybe_unused]] void rjust (std::string &str, const size_t &size, const char &c);
    [[maybe_unused]] void ljust (std::string &str, const size_t &size, const char &c);

    char        hex2dec             (const char &a);
    std::string escape_string       (const std::string &str);
    bool        valid_special_symbol(const char &c);
    bool        escape_char_need    (const char &c);

    std::string generate_cache_name (const std::string &basename, const std::string &ext);

    // random

    size_t      random              (const size_t &_min, const size_t &_max);
    std::string random_string       (const size_t &len);

    std::string time                (const std::string &fmt, bool local = false);

    std::vector <replace_founded_item> found_replacers_in_file (const std::string &path, const size_t &start, const size_t &size, const MAP_STR_STR &replacers);

    // ====================[ Classes ]============================

    class manapi_exception : public std::exception {
        public:
            explicit manapi_exception (std::string message_);
            [[nodiscard]] const char * what() const noexcept override;
        private:
            std::string message;
    };
}

namespace manapi::net {
    struct request_data_t {
        size_t                              headers_size;
        std::map<std::string, std::string>  headers,
                                            params;

        std::string                         method;
        std::string                         uri;
        std::string                         http;
        std::vector <std::string>           path;
        size_t                              divided;

        char*                               body_ptr;
        size_t                              body_index;
        size_t                              body_left;
        size_t                              body_size;

        bool                                has_body    = false;
    };
}

#endif //MANAPIHTTP_MANAPIUTILS_H
