#pragma once

#include <string>
#include <map>

#include "../ManapiUtils.hpp"
#include "../ManapiAsync.hpp"
#include "../async/ManapiAsyncContext.hpp"
#include "../compress/ManapiCompress.hpp"
#include "../components/Buffer.hpp"
#include "../components/ManapiObjectPool.hpp"

namespace manapi::net::http {
    struct response_features_t {
        std::string compress;
        std::move_only_function<future<void>(std::string src, std::string dest)> *compressor_for_file = nullptr;
        std::move_only_function<std::string(std::string_view data)> *compressor_for_string = nullptr;
        std::unique_ptr<std::map <std::string, std::string>> replacers;
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
        // just headers
        std::map<std::string, std::string> headers;
        // contains params from url .../[param1]-[param2]/...
        std::map<std::string, std::string> params;

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

        int flags;

        bytebuffer buffer{};
    };

    struct replace_founded_item {
        std::string key;
        const std::string *value;
        std::pair <ssize_t, ssize_t> pos;
    };

    typedef std::map <std::string, std::vector <header_value_t> > headsers_t;

    void request_data_clear (request_data_t &data);
    std::vector <header_value_t> parse_header_value (std::string_view header_value);
    std::pair<std::string_view, std::string_view> parse_header (std::string_view header);
    std::string stringify_header (const std::pair<std::string, std::string> &header);
    std::string stringify_header_value (const std::vector <header_value_t> &header_value);
    future<std::vector <replace_founded_item>> found_replacers_in_file (const async::shared_cthread &ctx, const std::string &path, const ssize_t &start, const size_t &size, const std::map<std::string, std::string> &replacers);
}
