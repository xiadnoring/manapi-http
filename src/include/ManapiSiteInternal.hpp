#pragma once

#include "http/ManapiSite.hpp"

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


struct manapi::net::http::site::data_t {
    std::shared_ptr<multithread_storage::worker_t> server_config;
    std::shared_ptr<manapi::json> config_;
    server_ctx sctx;
    http_uri_part handlers;
    std::unique_ptr<std::map <std::string, compress_file_cb_t, std::less<>>> compressors_for_file{};
    std::unique_ptr<std::map <std::string, compress_str_cb_t, std::less<>>> compressors_for_string{};
    std::unique_ptr<std::map <std::string, std::map <std::string, implement_create_cb>>> transport_protocol_workers{};
    std::unique_ptr<std::map <http::versions::http, std::map <std::string, implemenet_http_cb>>> http_protocol_workers{};
    std::mutex loopmx{};
};

struct manapi::net::http::http_handler_function {
    handler_template_t handler = nullptr;
    ssize_t trailers_size = 4096;
    std::set<std::string> trailers;
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