#include <format>
#include <fstream>
#include <utility>
#include "ManapiHttpResponse.hpp"

#include "ManapiAsync.hpp"
#include "services/ManapiFetch.hpp"
#include "ManapiUtils.hpp"
#include "ManapiHttpRequest.hpp"
#include "ManapiHttpTypes.hpp"
#include "ManapiHttpMime.hpp"

manapi::net::http::response::response(manapi::net::http::request_data_t &request_data, const size_t &_status, http::config &config): config_(config), status_code_(_status), http_version_("1.1") {
    this->request_data_ = &request_data;
    this->type_ = RESPONSE_NO_DATA;

    detect_ranges ();
}

manapi::net::http::response::~response() {
    // delete custom data if it exists
    clear_custom_data();
}


void manapi::net::http::response::header(const std::string &key, std::string value) {
    this->headers_[key] = std::move(value);
}

void manapi::net::http::response::remove_header(const std::string &key) {
    this->headers_.erase(key);
}

bool manapi::net::http::response::has_header(const std::string &key) {
    return this->headers_.contains(key);
}

/**
 * if key exists returning the pointer to the string otherwise returning nullptr
 * @param key the key of the header
 * @return the pointer to the string | nullptr
 */
const std::string &manapi::net::http::response::header(const std::string &key) {
    if (this->headers_.contains(key))
    {
        return this->headers_[key];
    }

    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_HEADER_MISSING, "The header '{}' could not be found", key);
}

void manapi::net::http::response::text(std::string plain_text) {
    this->data_ = std::move(plain_text);
    this->type_ = RESPONSE_TEXT;
}

void manapi::net::http::response::json(manapi::json data, const size_t &spaces) {
    header(HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON);
    text(std::move(data.dump (static_cast<int>(spaces))));
}

void manapi::net::http::response::form(formdata_send formdata) {
    this->type_ = RESPONSE_FORMDATA;
    this->formdata_ = std::move(formdata);
}

void manapi::net::http::response::status_code(const size_t &status_code) {
    this->status_code_ = status_code;
}

void manapi::net::http::response::status(const size_t &_status_code) {
    this->status_code_ = _status_code;
}

void manapi::net::http::response::file(std::string path) {
    this->data_ = std::move(path);
    this->type_ = RESPONSE_FILE;
}

bool manapi::net::http::response::is_file() const {
    return this->type_ == RESPONSE_FILE;
}

bool manapi::net::http::response::is_text() const {
    return this->type_ == RESPONSE_TEXT;
}

bool manapi::net::http::response::is_proxy() const {
    return this->type_ == RESPONSE_PROXY;
}

bool manapi::net::http::response::is_no_data() const {
    return this->type_ == RESPONSE_NO_DATA;
}

bool manapi::net::http::response::is_formdata() const {
    return this->type_ == RESPONSE_FORMDATA;
}

bool manapi::net::http::response::has_ranges() const {
    return !this->ranges_.empty();
}

const std::string &manapi::net::http::response::file() {
    return this->data_;
}

const std::string &manapi::net::http::response::http_version() {
    return this->http_version_;
}

const size_t &manapi::net::http::response::status_code() const {
    return this->status_code_;
}

std::string_view manapi::net::http::response::status_message() {
    return status_to_string(this->status_code_);
}

std::map<std::string, std::string> manapi::net::http::response::headers() {
    return std::move(this->headers_);
}

std::string &manapi::net::http::response::body() {
    return this->data_;
}

const std::map<std::string, std::string> & manapi::net::http::response::ref_headers() {
    return this->headers_;
}

void manapi::net::http::response::compress(const std::string &name) {
    if (this->compress_enabled_)
    {
        this->compress_ = name;
    }
}

void manapi::net::http::response::compress_enabled (const bool &status) {
    this->compress_enabled_ = status;
    this->compress_ = "";
}

const std::string &manapi::net::http::response::compress() {
    if (this->compress_enabled_ && this->compress_.empty() && this->request_data_->headers.contains(HEADER.ACCEPT_ENCODING)) {
        std::string *value = &this->request_data_->headers.at(HEADER.ACCEPT_ENCODING);
        const auto data = http::parse_header_value(*value);

        for (const auto &a: data) {
            if (this->config_.contains_compressor(a.value)) {
                this->compress_ = a.value;
                break;
            }
        }
    }

    return this->compress_;
}

void manapi::net::http::response::detect_ranges () {
    if (!this->request_data_->headers.contains(HEADER.RANGE))
    {
        return;
    }

    const auto values = http::parse_header_value(this->request_data_->headers.at(HEADER.RANGE));

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

            this->ranges_.emplace_back(first, second);
        }
    }
}

bool manapi::net::http::response::partial_enabled() const {
    return this->partial_enabled_;
}

std::optional<std::map<std::string, std::string>> manapi::net::http::response::replacers() {
    return std::move(this->replacers_);
}

void manapi::net::http::response::custom_data(custom_data_t data) {
    clear_custom_data();

    this->custom_data_ = std::move(data);
}

void manapi::net::http::response::clear_custom_data() {
    if (this->custom_data_.src != nullptr) {
        this->custom_data_.clean (this->custom_data_.src);

        this->custom_data_.src = nullptr;
        this->custom_data_.clean = nullptr;
    }
}

manapi::net::http::custom_data_t & manapi::net::http::response::custom_data() {
    return this->custom_data_;
}

void manapi::net::http::response::replacers(std::map<std::string, std::string> replacers) {
    compress_enabled (false);
    partial_status (false);

    this->replacers_ = std::move(replacers);
}

void manapi::net::http::response::partial_status(const bool &auto_partial_status) {
    //if (has_ranges())
    //{
        this->partial_enabled_ = auto_partial_status;
    //}
}

void manapi::net::http::response::proxy(std::string url) {
    this->type_ = RESPONSE_PROXY;
    this->data_ = std::move(url);

    this->compress_enabled(false);
}

void manapi::net::http::response::proxy(std::string url, std::function<void(manapi::net::fetch &)> cb) {
    this->proxy(std::move(url));
    this->proxy_setup = std::move(cb);
}

const std::string &manapi::net::http::response::data() {
    return this->data_;
}

manapi::net::formdata_send manapi::net::http::response::formdata() {
    if (this->type_ != RESPONSE_FORMDATA) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_HTTP_PROTOCOL_ERROR, "formdata is missing in the response. type = {}", static_cast<int>(this->type_));
    }
    this->type_ = RESPONSE_NO_DATA;
    auto data = std::move(this->formdata_.value());
    this->formdata_.reset();
    return std::move(data);
}

std::function<void(manapi::net::fetch &)> manapi::net::http::response::proxy_setup_cb() {
    return std::move(this->proxy_setup.value_or(nullptr));
}
