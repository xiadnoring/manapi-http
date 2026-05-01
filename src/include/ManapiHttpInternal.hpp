#pragma once

#include "ManapiHttp.hpp"

struct manapi::net::http::http_uri_part {
    std::unique_ptr<handlers_map_t>             map = nullptr;
    // params
    std::unique_ptr<handlers_regex_titles_t>    params = nullptr;
    // functions (handler, post_mask, get_mask) by method
    std::unique_ptr<handlers_types_t>           handlers = nullptr;
    // errors
    std::unique_ptr<handlers_types_t>           errors = nullptr;
    // layers
    std::unique_ptr<handlers_types_t>           layers = nullptr;
    // static handler functions
    std::unique_ptr<handlers_static_types_t>    statics = nullptr;
    // regex handler functions
    std::unique_ptr<handlers_regex_map_t>       regexes = nullptr;
};

struct manapi::net::http::http_handler_function {
    handler_template_t handler = nullptr;
    uint8_t flags = 0;
    ssize_t trailers_size = 4096;
    std::set<std::string, std::less<>> trailers;
};

struct manapi::net::http::http_static_handler_function {
    std::string folder;
    std::shared_ptr<http_handler_function> layer;
};

struct manapi::net::http::http_handler_page {
    std::shared_ptr<http_handler_function>                      handler = nullptr;
    std::unique_ptr<http_handler_page>                          error = nullptr;
    std::vector<std::shared_ptr<http_handler_function>>         layer;
    http_static_handler_function                                *statics = nullptr;
    size_t                                                      statics_parts_len{};
};

enum http_handler_function_flags {
    HTTP_HANDLER_FUNC_FLAG_CUSTOM = 1
};