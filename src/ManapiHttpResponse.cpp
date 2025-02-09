#include <format>
#include <fstream>
#include <utility>
#include "ManapiHttpResponse.hpp"

#include "ManapiAsync.hpp"
#include "ManapiUtils.hpp"
#include "ManapiHttpRequest.hpp"
#include "ManapiHttpTypes.hpp"
#include "ManapiHttpMime.hpp"

manapi::net::http_response::http_response(manapi::net::http::request_data_t &request_data, const size_t &_status, std::string message, http::config &config): config(config), status_code(_status), status_message(std::move(message)), http_version("1.1") {
    this->request_data = &request_data;
    this->type = RESPONSE_NO_DATA;

    detect_ranges ();
}

manapi::net::http_response::~http_response() {
    // delete custom data if it exists
    clear_custom_data();
}


void manapi::net::http_response::set_header(const std::string &key, std::string value) {
    this->headers[key] = std::move(value);
}

void manapi::net::http_response::remove_header(const std::string &key) {
    this->headers.erase(key);
}

bool manapi::net::http_response::has_header(const std::string &key) {
    return this->headers.contains(key);
}

/**
 * if key exists returning the pointer to the string otherwise returning nullptr
 * @param key the key of the header
 * @return the pointer to the string | nullptr
 */
const std::string &manapi::net::http_response::get_header(const std::string &key) {
    if (this->headers.contains(key))
    {
        return this->headers[key];
    }

    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_HEADER_MISSING, "The header '{}' could not be found", key);
}

void manapi::net::http_response::text(std::string plain_text) {
    this->data = std::move(plain_text);
    this->type = RESPONSE_TEXT;
}

void manapi::net::http_response::json(const manapi::json& data, const size_t &spaces) {
    set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON);
    text(std::move(data.dump (static_cast<int>(spaces))));
}

void manapi::net::http_response::set_status_code(const size_t &_status_code) {
    this->status_code = _status_code;
}

void manapi::net::http_response::set_status_message(const std::string &_status_message) {
    this->status_message = _status_message;
}

void manapi::net::http_response::set_status(const size_t &_status_code, const std::string &_status_message) {
    this->status_code = _status_code;
    this->status_message = _status_message;
}

void manapi::net::http_response::file(std::string path) {
    this->data = std::move(path);
    this->type = RESPONSE_FILE;
}

bool manapi::net::http_response::is_file() const {
    return this->type == RESPONSE_FILE;
}

bool manapi::net::http_response::is_text() const {
    return this->type == RESPONSE_TEXT;
}

bool manapi::net::http_response::is_proxy() const {
    return this->type == RESPONSE_PROXY;
}

bool manapi::net::http_response::is_no_data() const {
    return this->type == RESPONSE_NO_DATA;
}

bool manapi::net::http_response::has_ranges() const {
    return !this->ranges.empty();
}

const std::string &manapi::net::http_response::get_file() {
    return this->data;
}

const std::string &manapi::net::http_response::get_http_version() {
    return this->http_version;
}

const size_t &manapi::net::http_response::get_status_code() const {
    return this->status_code;
}

const std::string &manapi::net::http_response::get_status_message() {
    return this->status_message;
}

std::map<std::string, std::string> manapi::net::http_response::get_headers() {
    return std::move(this->headers);
}

std::string &manapi::net::http_response::get_body() {
    return this->data;
}

const std::map<std::string, std::string> & manapi::net::http_response::ref_headers() {
    return this->headers;
}

void manapi::net::http_response::set_compress(const std::string &name) {
    if (this->compress_enabled)
    {
        this->compress = name;
    }
}

void manapi::net::http_response::set_compress_enabled (const bool &status) {
    this->compress_enabled = status;
    this->compress = "";
}

const std::string &manapi::net::http_response::get_compress() {
    if (this->compress_enabled && this->compress.empty() && this->request_data->headers.contains(HTTP_HEADER.ACCEPT_ENCODING)) {
        std::string *value = &this->request_data->headers.at(HTTP_HEADER.ACCEPT_ENCODING);
        const auto data = http::parse_header_value(*value);

        for (const auto &a: data) {
            if (this->config.contains_compressor(a.value)) {
                this->compress = a.value;
                break;
            }
        }
    }

    return this->compress;
}

void manapi::net::http_response::detect_ranges () {
    if (!this->request_data->headers.contains(HTTP_HEADER.RANGE))
    {
        return;
    }

    const auto values = http::parse_header_value(this->request_data->headers.at(HTTP_HEADER.RANGE));

    for (const auto& value: values) {
        if (value.params.contains("bytes")) {
            const std::string &range_str = value.params.at("bytes");
            size_t pos_delimiter = range_str.find ('-');

            if (pos_delimiter == std::string::npos)
            {
                THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_HEADER_INVALID, "invalid the header range: {}", range_str);
            }

            // trans 2 size_t range
            const char *end_ptr     = range_str.data() + pos_delimiter;
            const ssize_t first     = pos_delimiter > 0                        ? std::strtoll(range_str.data(), const_cast<char **>(&end_ptr), 10) : -1;
            const ssize_t second    = pos_delimiter < range_str.size() - 1    ? std::strtoll(end_ptr, nullptr, 10) : -1;

            this->ranges.emplace_back(first, second);
        }
    }
}

bool manapi::net::http_response::get_partial_enabled() const {
    return this->partial_enabled;
}

std::optional<std::map<std::string, std::string>> manapi::net::http_response::get_replacers() {
    return std::move(this->replacers);
}

void manapi::net::http_response::set_custom_data(custom_data_t data) {
    clear_custom_data();

    this->custom_data = std::move(data);
}

void manapi::net::http_response::clear_custom_data() {
    if (this->custom_data.src != nullptr) {
        this->custom_data.clean (this->custom_data.src);

        this->custom_data.src = nullptr;
        this->custom_data.clean = nullptr;
    }
}

manapi::net::custom_data_t & manapi::net::http_response::get_custom_data() {
    return this->custom_data;
}

void manapi::net::http_response::set_replacers(std::map<std::string, std::string> _replacers) {
    set_compress_enabled(false);
    set_partial_status  (false);

    this->replacers = std::move(_replacers);
}

void manapi::net::http_response::set_partial_status(const bool &auto_partial_status) {
    //if (has_ranges())
    //{
        this->partial_enabled = auto_partial_status;
    //}
}

void manapi::net::http_response::proxy(std::string url) {
    this->type = RESPONSE_PROXY;
    this->data = std::move(url);

    this->set_compress_enabled(false);
}

void manapi::net::http_response::proxy(std::string url, std::function<void(manapi::net::fetch &)> cb) {
    this->proxy(std::move(url));
    this->proxy_setup = std::move(cb);
}

const std::string &manapi::net::http_response::get_data() {
    return this->data;
}

std::function<void(manapi::net::fetch &)> manapi::net::http_response::get_proxy_setup_cb() {
    return std::move(this->proxy_setup.value_or(nullptr));
}
