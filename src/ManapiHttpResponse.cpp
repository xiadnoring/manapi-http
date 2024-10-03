#include <format>
#include <fstream>
#include <utility>
#include "ManapiHttpResponse.hpp"
#include "ManapiUtils.hpp"
#include "ManapiHttpRequest.hpp"
#include "ManapiHttpTypes.hpp"

manapi::net::http_response::http_response(manapi::net::request_data_t &_request_data, const size_t &_status, std::string _message, api::pool *tasks, class config *config): status_code(_status), status_message(std::move(_message)), http_version("1.1") {
    this->config = config;
    this->tasks = tasks;

    request_data    = &_request_data;

    type            = MANAPIHTTP_RESP_NO_DATA;

    detect_ranges ();
}

manapi::net::http_response::~http_response() {
    delete tasks;

    // if replacers exists
    if (replacers != nullptr)
    {
        delete replacers;
    }
}


void manapi::net::http_response::set_header(const std::string &key, const std::string &value) {
    headers[key] = value;
}

void manapi::net::http_response::remove_header(const std::string &key) {
    headers.erase(key);
}

bool manapi::net::http_response::has_header(const std::string &key) {
    return headers.contains(key);
}

/**
 * if key exists returning the pointer to the string otherwise returning nullptr
 * @param key the key of the header
 * @return the pointer to the string | nullptr
 */
const std::string &manapi::net::http_response::get_header(const std::string &key) {
    if (headers.contains(key))
    {
        return headers[key];
    }

    THROW_MANAPI_EXCEPTION("The header '{}' could not be found", key);
}

void manapi::net::http_response::text(const std::string &plain_text) {
    data            = plain_text;
    type            = MANAPI_HTTP_RESP_TEXT;
}

void manapi::net::http_response::json(const class json &jp, const size_t &spaces) {
    set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON);
    text(jp.dump (spaces));
}

void manapi::net::http_response::set_status_code(const size_t &_status_code) {
    status_code     = _status_code;
}

void manapi::net::http_response::set_status_message(const std::string &_status_message) {
    status_message  = _status_message;
}

void manapi::net::http_response::set_status(const size_t &_status_code, const std::string &_status_message) {
    status_code     = _status_code;
    status_message  = _status_message;
}

void manapi::net::http_response::file(const std::string &path) {
    data                = path;
    type                = MANAPI_HTTP_RESP_FILE;
}

bool manapi::net::http_response::is_file() const {
    return type == MANAPI_HTTP_RESP_FILE;
}

bool manapi::net::http_response::is_text() const {
    return type == MANAPI_HTTP_RESP_TEXT;
}

bool manapi::net::http_response::is_proxy() const {
    return type == MANAPI_HTTP_RESP_PROXY;
}

bool manapi::net::http_response::is_no_data() const {
    return type == MANAPIHTTP_RESP_NO_DATA;
}

bool manapi::net::http_response::has_ranges() const {
    return !ranges.empty();
}

const std::string &manapi::net::http_response::get_file() {
    return data;
}

const std::string &manapi::net::http_response::get_http_version() {
    return http_version;
}

const size_t &manapi::net::http_response::get_status_code() const {
    return status_code;
}

const std::string &manapi::net::http_response::get_status_message() {
    return status_message;
}

const std::map<std::string, std::string> &manapi::net::http_response::get_headers() {
    return headers;
}

const std::string &manapi::net::http_response::get_body() {
    return data;
}

void manapi::net::http_response::set_compress(const std::string &name) {
    if (compress_enabled)
    {
        this->compress = name;
    }
}

void manapi::net::http_response::set_compress_enabled (const bool &status) {
    compress_enabled    = status;
    compress            = "";
}

const std::string &manapi::net::http_response::get_compress() {
    if (compress_enabled && compress.empty() && request_data->headers.contains(HTTP_HEADER.ACCEPT_ENCODING)) {
        std::string *value = &request_data->headers.at(HTTP_HEADER.ACCEPT_ENCODING);
        const auto data = utils::parse_header_value(*value);

        for (const auto &a: data) {
            if (config->contains_compressor(a.value)) {
                compress = a.value;
                break;
            }
        }
    }

    return compress;
}

void manapi::net::http_response::detect_ranges () {
    if (!request_data->headers.contains(HTTP_HEADER.RANGE))
    {
        return;
    }

    const auto values = utils::parse_header_value(request_data->headers.at(HTTP_HEADER.RANGE));

    for (const auto& value: values) {
        if (value.params.contains("bytes")) {
            const std::string &range_str = value.params.at("bytes");
            size_t pos_delimiter = range_str.find ('-');

            if (pos_delimiter == std::string::npos)
            {
                THROW_MANAPI_EXCEPTION("invalid the header range: {}", range_str);
            }

            // trans 2 size_t range
            const char *end_ptr     = range_str.data() + pos_delimiter;
            const ssize_t first     = pos_delimiter > 0                        ? std::strtoll(range_str.data(), const_cast<char **>(&end_ptr), 10) : -1;
            const ssize_t second    = pos_delimiter < range_str.size() - 1    ? std::strtoll(end_ptr, nullptr, 10) : -1;

            ranges.emplace_back(first, second);
        }
    }
}

bool manapi::net::http_response::get_partial_enabled() const {
    return partial_enabled;
}

const manapi::net::utils::MAP_STR_STR *manapi::net::http_response::get_replacers() {
    return replacers;
}

void manapi::net::http_response::set_replacers(const utils::MAP_STR_STR &_replacers) {
    set_compress_enabled(false);
    set_partial_status  (false);

    replacers = new utils::MAP_STR_STR (_replacers);
}

void manapi::net::http_response::set_partial_status(const bool &auto_partial_status) {
    if (has_ranges())
    {
        partial_enabled = auto_partial_status;
    }
}

void manapi::net::http_response::proxy(const std::string &url) {
    type = MANAPI_HTTP_RESP_PROXY;
    data = url;

    set_compress_enabled(false);
}

const std::string &manapi::net::http_response::get_data() {
    return data;
}
