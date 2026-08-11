#pragma once

#include "ManapiHttp.hpp"
#include "std/ManapiRef.hpp"

namespace manapi::net::http {

    struct http_handler_function {
        handler_template_t handler = nullptr;
        uint8_t flags = 0;
        ssize_t trailers_size = 4096;
        std::unordered_set<std::string, manapi::text_hash, std::equal_to<>> trailers;
        uint32_t refcnt = 0;
    };

    struct http_handler_error {
        manapi::reference<http_handler_function> handler;
        std::size_t layer_depth ;
    };

    struct http_static_handler_function {
        std::string folder;
        manapi::reference<http_handler_function> layer;
        uint32_t refcnt;
    };

    struct http_handler_page {
        manapi::reference<http_handler_function> handler;
        std::vector<http_handler_error> error;
        std::vector<manapi::reference<http_handler_function>> layer;
        manapi::reference<http_static_handler_function> statics;
        std::size_t statics_parts_len;
    };

    typedef std::unordered_map<std::string, std::unique_ptr<http_uri_part>, manapi::text_hash, std::equal_to<>> handlers_map_t;

    typedef std::pair<std::regex, std::unique_ptr<http_uri_part>> handlers_regex_pair_t;

    typedef std::unordered_map<std::string, handlers_regex_pair_t, manapi::text_hash, std::equal_to<>> handlers_regex_map_t;

    typedef std::vector<std::string> handlers_regex_titles_t;

    typedef std::unordered_map<std::string, manapi::reference<http_static_handler_function>, manapi::text_hash, std::equal_to<>> handlers_static_types_t;

    typedef std::unordered_map<std::string, manapi::reference<http_handler_function>, manapi::text_hash, std::equal_to<>> handlers_types_t;

    struct http_uri_part {
        std::unique_ptr<handlers_map_t> map = nullptr;
        // params
        std::unique_ptr<handlers_regex_titles_t> params = nullptr;
        // functions (handler, post_mask, get_mask) by method
        std::unique_ptr<handlers_types_t> handlers = nullptr;
        // errors
        std::unique_ptr<handlers_types_t> errors = nullptr;
        // layers
        std::unique_ptr<handlers_types_t> layers = nullptr;
        // static handler functions
        std::unique_ptr<handlers_static_types_t> statics = nullptr;
        // regex handler functions
        std::unique_ptr<handlers_regex_map_t> regexes = nullptr;
    };
}

enum http_handler_function_flags {
    HTTP_HANDLER_FUNC_FLAG_CUSTOM = 1
};

