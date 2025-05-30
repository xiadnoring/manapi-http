#include <format>
#include <fstream>
#include <utility>

#include "include/ManapiHttpStructs.hpp"
#include "ManapiHttpResponse.hpp"
#include "ManapiAsync.hpp"
#include "services/ManapiFetch.hpp"
#include "ManapiUtils.hpp"
#include "ManapiHttpRequest.hpp"
#include "ManapiHttpTypes.hpp"
#include "ManapiHttpMime.hpp"

manapi::net::http::response::response(manapi::net::http::request_data_t * request_data, int status, http::config *config): config_(config), status_code_(status) {
    this->request_data_ = request_data;
    this->type_ = internal::RESPONSE_NO_DATA;
    this->flags = 0;
    this->data_ = nullptr;
    this->compress_ = nullptr;

    /* TODO: remove */
    this->detect_ranges ();
}

manapi::net::http::response::~response() {
    switch (this->type_) {
        case internal::RESPONSE_FORMDATA:
            delete static_cast<formdata_send *> (this->data_);
            delete static_cast<char *> (this->data_);
        break;
        case internal::RESPONSE_SYNC_CALLBACK:
            delete static_cast<resp_callback_sync *> (this->data_);
        break;
        case internal::RESPONSE_ASYNC_CALLBACK:
            delete static_cast<resp_callback_async *> (this->data_);
        break;
        case internal::RESPONSE_FILE:
        case internal::RESPONSE_PROXY:
        case internal::RESPONSE_TEXT:
            delete static_cast<std::string *> (this->data_);
        break;
        default:
            break;
    }
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
    auto storage = std::make_unique<std::string>(std::move(plain_text));

    this->type_ = internal::RESPONSE_TEXT;
    this->data_ = storage.release();
}

void manapi::net::http::response::json(manapi::json data, const size_t &spaces) {
    header(HEADER.CONTENT_TYPE, manapi::mime::types.APPLICATION_JSON);
    text(std::move(data.dump (static_cast<int>(spaces))));
}

void manapi::net::http::response::form(formdata_send formdata) {
    auto storage = std::make_unique<formdata_send>(std::move(formdata));

    this->data_ = storage.release();
    this->type_ = internal::RESPONSE_FORMDATA;
}

void manapi::net::http::response::status_code(const size_t &status_code) {
    this->status_code_ = status_code;
}

void manapi::net::http::response::status(const size_t &_status_code) {
    this->status_code_ = _status_code;
}

void manapi::net::http::response::file(std::string path) {
    auto storage = std::make_unique<std::string>(std::move(path));

    this->type_ = internal::RESPONSE_FILE;
    this->data_ = storage.release();
}

bool manapi::net::http::response::is_file() const {
    return this->type_ == internal::RESPONSE_FILE;
}

bool manapi::net::http::response::is_text() const {
    return this->type_ == internal::RESPONSE_TEXT;
}

bool manapi::net::http::response::is_proxy() const {
    return this->type_ == internal::RESPONSE_PROXY;
}

bool manapi::net::http::response::is_no_data() const {
    return this->type_ == internal::RESPONSE_NO_DATA;
}

bool manapi::net::http::response::is_formdata() const {
    return this->type_ == internal::RESPONSE_FORMDATA;
}

bool manapi::net::http::response::is_async_cb() const {
    return this->type_ == internal::RESPONSE_ASYNC_CALLBACK;
}

bool manapi::net::http::response::is_sync_cb() const {
    return this->type_ == internal::RESPONSE_SYNC_CALLBACK;
}

bool manapi::net::http::response::has_ranges() const {
    return this->ranges_ && !this->ranges_->empty();
}

std::string &manapi::net::http::response::file() {
    this->check_type_(internal::RESPONSE_FILE);
    return this->body();
}

int manapi::net::http::response::status_code() const {
    return this->status_code_;
}

std::string_view manapi::net::http::response::status_message() {
    return status_to_string(this->status_code_);
}

std::map<std::string, std::string> &manapi::net::http::response::headers() {
    return this->headers_;
}

std::string &manapi::net::http::response::body() {
    return *static_cast<std::string *> (this->data_);
}

void manapi::net::http::response::compress(std::string name) {
    this->compress_ = std::make_unique<std::string>(std::move(name));
}

void manapi::net::http::response::compress_enabled (bool state) {
    if (state)
        this->flags |= internal::RESPONSE_FLAG_COMPRESS_ENABLED;
    else if (this->flags & internal::RESPONSE_FLAG_COMPRESS_ENABLED)
        this->flags ^= internal::RESPONSE_FLAG_COMPRESS_ENABLED;
}

std::string manapi::net::http::response::compress() {
    std::string compress;


    if ((this->flags & internal::RESPONSE_FLAG_COMPRESS_ENABLED)) {
        auto it = this->request_data_->headers.find(HEADER.ACCEPT_ENCODING);
        if (it != this->request_data_->headers.end()) {

            if (this->compress_) {
                compress = *this->compress_;
            }

            if (it->second.size() < 1000) {
                /* lite check */

                if (compress.empty()) {
                    auto data = http::parse_header_value(it->second);
                    for (auto &a: data) {
                        if (this->config_->contains_compressor(a.value)) {
                            compress = std::move(a.value);
                            break;
                        }
                    }
                }
            }
        }
    }

    return std::move(compress);
}

void manapi::net::http::response::detect_ranges () {
    if (this->ranges_) {
        return;
    }

    auto it = this->request_data_->headers.find(HEADER.RANGE);
    if (it == this->request_data_->headers.end()) {
        return;
    }

    this->ranges_ = std::make_unique<decltype(this->ranges_)::element_type>();

    const auto values = http::parse_header_value(it->second);

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

            this->ranges_->emplace_back(first, second);
        }
    }
}

bool manapi::net::http::response::partial_enabled() const {
    return (this->flags & internal::RESPONSE_FLAG_PARTITIAL_ENABLED);
}

int manapi::net::http::response::data_type() const {
    return this->type_;
}

std::unique_ptr<std::map<std::string, std::string>> &manapi::net::http::response::replacers() {
    return this->replacers_;
}

void manapi::net::http::response::custom_data(custom_data_t data) {
    this->custom_data_.reset(new custom_data_t (std::move(data)));
}

manapi::net::http::config *manapi::net::http::response::config() {
    return this->config_;
}

manapi::net::http::custom_data_t *manapi::net::http::response::custom_data() {
    return this->custom_data_.get();
}

manapi::net::http::response::resp_callback_async& manapi::net::http::response::callback_async() {
    this->check_type_(internal::RESPONSE_ASYNC_CALLBACK);
    return *static_cast<resp_callback_async *> (this->data_);
}

manapi::net::http::response::resp_callback_sync &manapi::net::http::response::callback_sync() {
    this->check_type_(internal::RESPONSE_SYNC_CALLBACK);
    return *static_cast<resp_callback_sync *> (this->data_);
}

manapi::net::http::request_data_t * manapi::net::http::response::request_data() {
    return this->request_data_;
}

void manapi::net::http::response::check_type_(int type) {
    if (this->type_ != type || !this->data_) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_HTTP_PROTOCOL_ERROR, "data is missing in the response. type = {}", static_cast<int>(this->type_));
    }
}

void manapi::net::http::response::replacers(std::map<std::string, std::string> replacers) {
    this->compress_enabled (false);
    this->partial_enabled (false);

    this->replacers_ = std::make_unique<decltype(replacers)>(std::move(replacers));
}

void manapi::net::http::response::partial_enabled(bool state) {
    if (state)
        this->flags |= internal::RESPONSE_FLAG_PARTITIAL_ENABLED;
    else if ((this->flags & internal::RESPONSE_FLAG_PARTITIAL_ENABLED))
        this->flags ^= internal::RESPONSE_FLAG_PARTITIAL_ENABLED;
}
#ifdef MANAPIHTTP_FETCH_SUPPORT
void manapi::net::http::response::proxy(std::string url) {
    auto storage = std::make_unique<std::string>(std::move(url));
    this->type_ = internal::RESPONSE_PROXY;
    this->data_ = storage.release();

    this->compress_enabled(false);
}

void manapi::net::http::response::proxy(std::string url, resp_proxy_setup_cb cb) {
    this->proxy(std::move(url));
    this->proxy_setup = std::make_unique<decltype(this->proxy_setup)::element_type>(std::move(cb));
}
#endif

void manapi::net::http::response::callback_sync(resp_callback_sync cb) {
    auto storage = std::make_unique<resp_callback_sync>(std::move(cb));
    this->type_ = internal::RESPONSE_SYNC_CALLBACK;
    this->data_ = storage.release();
}

void manapi::net::http::response::callback_async(resp_callback_async cb) {
    auto storage = std::make_unique<resp_callback_async>(std::move(cb));
    this->type_ = internal::RESPONSE_ASYNC_CALLBACK;
    this->data_ = storage.release();
}

std::string &manapi::net::http::response::text() {
    this->check_type_(internal::RESPONSE_TEXT);
    return this->body();
}

std::string &manapi::net::http::response::url() {
    this->check_type_(internal::RESPONSE_PROXY);
    return this->body();
}

std::unique_ptr<std::vector<std::pair<ssize_t, ssize_t>>> manapi::net::http::response::ranges() {
    return std::move(this->ranges_);
}

manapi::net::formdata_send &manapi::net::http::response::formdata() {
    this->check_type_(internal::RESPONSE_FORMDATA);
    return *static_cast<formdata_send *> (this->data_);
}

#ifdef MANAPIHTTP_FETCH_SUPPORT
std::unique_ptr<manapi::net::http::response::resp_proxy_setup_cb> &manapi::net::http::response::proxy_setup_cb() {
    return this->proxy_setup;
}
#endif
