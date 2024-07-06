#include <format>
#include <fstream>
#include <utility>
#include "ManapiHttpResponse.h"
#include "ManapiUtils.h"
#include "ManapiHttpRequest.h"
#include "ManapiHttpTypes.h"

manapi::net::http_response::http_response(manapi::net::request_data_t &_request_data, const size_t &_status, std::string _message, http *_http_server): http_server(_http_server), status_code(_status), status_message(std::move(_message)), http_version("1.1") {
    request_data = &_request_data;

    detect_ranges ();
}

manapi::net::http_response::~http_response() {
    // if replacers exists
    if (replacers != nullptr)
        delete replacers;
}


void manapi::net::http_response::set_header(const std::string &key, const std::string &value) {
    headers[key] = value;
}
/**
 * if key exists returning the pointer to the string otherwise returning nullptr
 * @param key the key of the header
 * @return the pointer to the string | nullptr
 */
std::string *manapi::net::http_response::get_header(const std::string &key) {
    try {
        if (headers.contains(key))
            return &headers[key];
    } catch (const std::exception &e) {
        MANAPI_LOG_ERROR(e, "The header '%s' could not be found", key.data());
    }
    return nullptr;
}

void manapi::net::http_response::text(const std::string &plain_text) {
    body            = plain_text;
}

void manapi::net::http_response::json(utils::json &jp, const size_t &spaces) {
    body            = jp.dump (spaces);
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
    sendfile                = path;
}

bool manapi::net::http_response::is_sendfile() {
    return !sendfile.empty();
}

std::string &manapi::net::http_response::get_sendfile() {
    return sendfile;
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
    return body;
}

void manapi::net::http_response::set_compress(const std::string &name) {
    if (compress_enabled)
        this->compress = name;
}

void manapi::net::http_response::set_compress_enabled (bool status) {
    compress_enabled    = status;
    compress            = "";
}

const std::string &manapi::net::http_response::get_compress() {
    if (compress_enabled && compress.empty() && request_data->headers.contains(http_header.ACCEPT_ENCODING)) {
        std::string *value = &request_data->headers.at(http_header.ACCEPT_ENCODING);
        const auto data = http_request::parse_header_value(*value);

        for (const auto &a: data) {
            if (http_server->contains_compressor(a.value)) {
                compress = a.value;
                break;
            }
        }
    }

    return compress;
}

void manapi::net::http_response::detect_ranges () {
    if (!request_data->headers.contains(http_header.RANGE))
        return;

    const auto values = manapi::net::http_request::parse_header_value(request_data->headers.at(http_header.RANGE));

    for (const auto& value: values) {
        if (value.params.contains("bytes")) {
            auto range_str = &value.params.at("bytes");
            size_t pos_delimiter = range_str->find ('-');

            if (pos_delimiter == std::string::npos)
                throw manapi::utils::manapi_exception ("invalid the header range");

            // trans 2 size_t range
            const char *end_ptr     = range_str->data() + pos_delimiter;
            const ssize_t first     = pos_delimiter > 0                        ? std::strtoll(range_str->data(), const_cast<char **>(&end_ptr), 10)    : -1;
            const ssize_t second    = pos_delimiter < range_str->size() - 1    ? std::strtoll(end_ptr, nullptr, 10)                                    : -1;

            ranges.emplace_back(first, second);
        }
    }
}

bool manapi::net::http_response::get_auto_partial_enabled() const {
    return auto_partial_enabled;
}

const manapi::utils::MAP_STR_STR *manapi::net::http_response::get_replacers() {
    return replacers;
}

void manapi::net::http_response::set_replacers(const utils::MAP_STR_STR &_replacers) {
    set_compress_enabled(false);
    set_partial_status  (false);

    replacers = new utils::MAP_STR_STR (_replacers);
}

void manapi::net::http_response::set_partial_status(bool auto_partial_status) {
    auto_partial_enabled = auto_partial_status;
}
