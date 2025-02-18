#pragma once

#include <string>
#include <map>

#include "../ManapiAsync.hpp"
#include "../async/ManapiAsyncContext.hpp"
#include "../compress/ManapiCompress.hpp"

namespace manapi::net::http {
    struct response_features_t {
        const std::string &compress;
        std::function<future<bool>(const std::string &src, const std::string &dest)> compressor = nullptr;
        std::optional<std::map <std::string, std::string>> replacers;
    };

    struct header_value_t {
        std::string value;
        std::map <std::string, std::string> params;
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
        // size of the part of the headers in the buffer (READ) [HHHH]BBBBBBB <- 4
        ssize_t headers_part;
        ssize_t headers_size;
        // just headers
        std::map<std::string, std::string> headers;
        // contains params from url .../[param1]-[param2]/...
        std::map<std::string, std::string> params;

        // GET, POST, HEAD
        std::string method;
        // PATH
        std::string uri;
        // version server
        std::string http;
        // split by '/'
        std::vector <std::string> path;
        // index of the element where URL get params in the path
        ssize_t divided;

        ssize_t body_index;
        ssize_t body_left;
        ssize_t body_size;
        // size of the part of the body in the buffer (READ) HHHH[BBBBB] <- 5
        ssize_t body_part;

        bool has_body    = false;

        std::string buffer;
        size_t buffer_size;
    };

    struct replace_founded_item {
        std::string key;
        const std::string *value;
        std::pair <ssize_t, ssize_t> pos;
    };

    typedef std::map <std::string, std::vector <header_value_t> > headsers_t;

    void request_data_clear (request_data_t &data);
    std::vector <header_value_t> parse_header_value (const std::string &header_value);
    std::pair<std::string, std::string> parse_header (const std::string &header);
    std::string stringify_header (const std::pair<std::string, std::string> &header);
    std::string stringify_header_value (const std::vector <header_value_t> &header_value);
    future<std::vector <replace_founded_item>> found_replacers_in_file (const std::shared_ptr<async::context> &ctx, const std::string &path, const ssize_t &start, const size_t &size, const std::map<std::string, std::string> &replacers);

}
