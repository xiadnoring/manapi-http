#pragma once

#include <string>
#include <map>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../ManapiMemoryPool.hpp"
#include "../compress/ManapiCompress.hpp"
#include "../std/ManapiAsyncContext.hpp"
#include "../std/ManapiBuffer.hpp"

namespace manapi::net::http {
    struct http_handler_function;

    struct response_features_t {
        typedef std::move_only_function<future<manapi::status>(std::string src, std::string dest)> compress_file_cb;
        typedef std::move_only_function<manapi::status_or<std::string>(std::string_view data)> compress_str_cb;

        std::string compress;
        compress_file_cb *compressor_for_file = nullptr;
        compress_str_cb *compressor_for_string = nullptr;
        std::unique_ptr<std::vector <std::pair<std::string, std::string>>> replacers;
    };

    struct header_value_t {
        std::string value;
        std::map <std::string, std::string> params;
    };

    struct header_value_view_t {
        std::string_view value;
        std::map <std::string_view, std::string_view> params;
    };

    struct manapi_socket_information
    {
        std::string ip;
        uint16_t port;

        bool operator==(const manapi_socket_information& other) const {
            return port == other.port && ip == other.ip;
        }
    };

    struct request_data_t {
        // just headers
        std::map<std::string, std::string, std::less<>> headers;
        // contains params from url .../[param1]-[param2]/...
        std::map<std::string, std::string, std::less<>> params;

        std::map<std::string, std::string, std::less<>> trailers;

        // GET, POST, HEAD
        std::string method;
        // PATH
        std::string uri;
        // version server
        int http;
        // split by '/'
        std::vector <std::string> path;
        // index of the element where URL get params in the path
        int divided;

        ssize_t body_size;

        uint32_t headers_size;

        uint32_t trailers_size;

        std::shared_ptr<http_handler_function> handler;

        int flags;
    };

    struct replace_founded_item {
        std::string key;
        const std::string *value;
        std::pair <ssize_t, ssize_t> pos;
    };

    typedef std::map <std::string, std::vector <header_value_t> > headsers_t;

    void request_data_clear (request_data_t &data);

    manapi::status_or<std::vector <header_value_t>> parse_header_value (std::string_view header_value) MANAPIHTTP_NOEXCEPT;

    manapi::status_or<std::pair<std::string_view, std::string_view>> parse_header (std::string_view header);

    std::string stringify_header (const std::pair<std::string_view, std::string_view> &header);

    std::size_t stringify_header (char *buff, const std::pair<std::string_view, std::string_view> &header);

    std::size_t stringify_header_size (const std::pair<std::string_view, std::string_view> &header);

    std::string stringify_header_value (const std::vector <header_value_view_t> &header_value);

    int version_ip_by_addr (const sockaddr *addr);

    bool header_has_more_fields (std::string_view name) MANAPIHTTP_NOEXCEPT;

    status_or<std::pair<std::string, uint16_t>> strinfigy_ip (const sockaddr *addr);

    status_or<uint16_t> port_by_addr (const sockaddr *addr);

    manapi::status ip_by_addr (const sockaddr *addr, char *arr);

    bool split_http_port (std::string_view name, std::string_view &host, std::string_view &port, bool& has_port);
}
