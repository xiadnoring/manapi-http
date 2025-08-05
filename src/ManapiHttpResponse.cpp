#include <format>
#include <fstream>
#include <utility>

#include "include/ManapiHttpStructs.hpp"
#include "ManapiHttpResponse.hpp"
#include "ManapiAsync.hpp"
#include "services/ManapiFetch.hpp"
#include "include/ManapiUtils.hpp"
#include "ManapiHttpRequest.hpp"
#include "ManapiHttpTypes.hpp"
#include "ManapiHttpMime.hpp"
#include "include/ManapiSiteInternal.hpp"
#include "http/ManapiBaseHttp.hpp"

void manapi::net::http::custom_data_deleter_t::operator()(custom_data_t *n) {
    try {
        if (n && n->clean)
            n->clean(n->src);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "custom_data_deleter_t", e.what());
    }
}

manapi::net::http::response::response(internal::handle_data_t *cdata, int status, http::config *config, std::unique_ptr<http::request> req):
    req_(std::move(req)), config_(config), status_code_(status) {
    this->cdata_ = cdata;
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

    if (this->cdata_)
        delete this->cdata_;
}


manapi::error::status manapi::net::http::response::header(const std::string &key, std::string value) MANAPIHTTP_NOEXCEPT {
    try {
        this->headers_.insert_or_assign(key, std::move(value));
        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

void manapi::net::http::response::remove_header(std::string_view key) MANAPIHTTP_NOEXCEPT {
    auto const it = this->headers_.find(key);
    if (it != this->headers_.end())
        this->headers_.erase(it);
}

bool manapi::net::http::response::contains_header(std::string_view key) const MANAPIHTTP_NOEXCEPT {
    return this->headers_.find(key) != this->headers_.end();
}

/**
 * if key exists returning the pointer to the string otherwise returning nullptr
 * @param key the key of the header
 * @return the pointer to the string | nullptr
 */
manapi::error::status_or<std::string_view> manapi::net::http::response::header(std::string_view key) MANAPIHTTP_NOEXCEPT {
    auto it = this->headers_.find(key);
    if (it == this->headers_.end())
        return error::status_not_found("headers:Not found");

    return std::string_view{it->second};
}

manapi::error::status manapi::net::http::response::text(std::string plain_text) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<std::string>(std::move(plain_text));

        this->type_ = internal::RESPONSE_TEXT;
        this->data_ = storage.release();
        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

manapi::error::status manapi::net::http::response::json(manapi::json data, size_t spaces) MANAPIHTTP_NOEXCEPT {
    try {
        auto res = header(std::string{HEADER.CONTENT_TYPE}, std::string{manapi::mime::types.APPLICATION_JSON});
        if (!res)
            return std::move(res);
        return text(std::move(data.dump (static_cast<int>(spaces))));
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

manapi::error::status manapi::net::http::response::form(formdata_send formdata) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<formdata_send>(std::move(formdata));

        this->data_ = storage.release();
        this->type_ = internal::RESPONSE_FORMDATA;
        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

void manapi::net::http::response::status_code(size_t status_code) MANAPIHTTP_NOEXCEPT {
    this->status_code_ = status_code;
}

void manapi::net::http::response::status(size_t _status_code) MANAPIHTTP_NOEXCEPT {
    this->status_code_ = _status_code;
}

manapi::error::status manapi::net::http::response::file(std::string path) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<std::string>(std::move(path));

        this->type_ = internal::RESPONSE_FILE;
        this->data_ = storage.release();
        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

bool manapi::net::http::response::is_file() const MANAPIHTTP_NOEXCEPT {
    return this->type_ == internal::RESPONSE_FILE;
}

bool manapi::net::http::response::is_text() const MANAPIHTTP_NOEXCEPT {
    return this->type_ == internal::RESPONSE_TEXT;
}

bool manapi::net::http::response::is_proxy() const MANAPIHTTP_NOEXCEPT {
    return this->type_ == internal::RESPONSE_PROXY;
}

bool manapi::net::http::response::is_no_data() const MANAPIHTTP_NOEXCEPT {
    return this->type_ == internal::RESPONSE_NO_DATA;
}

bool manapi::net::http::response::is_formdata() const MANAPIHTTP_NOEXCEPT {
    return this->type_ == internal::RESPONSE_FORMDATA;
}

bool manapi::net::http::response::is_async_cb() const MANAPIHTTP_NOEXCEPT {
    return this->type_ == internal::RESPONSE_ASYNC_CALLBACK;
}

bool manapi::net::http::response::is_sync_cb() const  MANAPIHTTP_NOEXCEPT{
    return this->type_ == internal::RESPONSE_SYNC_CALLBACK;
}

bool manapi::net::http::response::has_ranges() const MANAPIHTTP_NOEXCEPT {
    return this->ranges_ && !this->ranges_->empty();
}

manapi::error::status_or<std::string *> manapi::net::http::response::file() MANAPIHTTP_NOEXCEPT {
    auto err = this->check_type_(internal::RESPONSE_FILE);
    if (!err.ok())
        return std::move(err);
    return &this->body();
}

int manapi::net::http::response::status_code() const MANAPIHTTP_NOEXCEPT {
    return this->status_code_;
}

std::string_view manapi::net::http::response::status_message() MANAPIHTTP_NOEXCEPT {
    auto res = status_to_string(this->status_code_);
    if (res)
        return res.unwrap();
    return {};
}

std::map<std::string, std::string, std::less<>> &manapi::net::http::response::headers() MANAPIHTTP_NOEXCEPT {
    return this->headers_;
}

std::string &manapi::net::http::response::body() MANAPIHTTP_NOEXCEPT {
    return *static_cast<std::string *> (this->data_);
}

manapi::error::status manapi::net::http::response::compress(std::string name) MANAPIHTTP_NOEXCEPT {
    try {
        this->compress_ = std::make_unique<std::string>(std::move(name));
        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

void manapi::net::http::response::compress_enabled (bool state) MANAPIHTTP_NOEXCEPT {
    if (state)
        this->flags |= internal::RESPONSE_FLAG_COMPRESS_ENABLED;
    else if (this->flags & internal::RESPONSE_FLAG_COMPRESS_ENABLED)
        this->flags ^= internal::RESPONSE_FLAG_COMPRESS_ENABLED;
}

std::string manapi::net::http::response::compress() MANAPIHTTP_NOEXCEPT {
    std::string compress;


    if ((this->flags & internal::RESPONSE_FLAG_COMPRESS_ENABLED)) {
        auto it = this->cdata_->req_data->headers.find(HEADER.ACCEPT_ENCODING);
        if (it != this->cdata_->req_data->headers.end()) {

            if (this->compress_) {
                compress = std::move(*this->compress_);
            }

            if (it->second.size() < 1000) {
                /* lite check */

                if (compress.empty()) {
                    auto rhs = http::parse_header_value(it->second);
                    if (rhs.ok()) {
                        auto data = rhs.unwrap();
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
    }

    return std::move(compress);
}

void manapi::net::http::response::detect_ranges () MANAPIHTTP_NOEXCEPT {
    if (this->ranges_) {
        return;
    }

    auto it = this->cdata_->req_data->headers.find(HEADER.RANGE);
    if (it == this->cdata_->req_data->headers.end()) {
        return;
    }

    auto rhs = http::parse_header_value(it->second);
    if (!rhs.ok())
        return;

    try {
        auto ranges = std::make_unique<decltype(this->ranges_)::element_type>();

        const auto values = rhs.unwrap();
        for (const auto& value: values) {
            if (value.params.contains("bytes")) {
                const std::string &range_str = value.params.at("bytes");
                size_t pos_delimiter = range_str.find ('-');

                if (pos_delimiter == std::string::npos)
                    return;

                // trans 2 size_t range
                const char *end_ptr     = range_str.data() + pos_delimiter;
                const ssize_t first     = pos_delimiter > 0                        ? std::strtoll(range_str.data(), const_cast<char **>(&end_ptr), 10) : -1;
                const ssize_t second    = pos_delimiter < range_str.size() - 1    ? std::strtoll(end_ptr, nullptr, 10) : -1;

                ranges->emplace_back(first, second);
            }
        }

        this->ranges_ = std::move(ranges);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "resp:detect_ranges()", e.what());
    }
}

bool manapi::net::http::response::partial_enabled() const MANAPIHTTP_NOEXCEPT {
    return (this->flags & internal::RESPONSE_FLAG_PARTITIAL_ENABLED);
}

int manapi::net::http::response::data_type() const MANAPIHTTP_NOEXCEPT {
    return this->type_;
}

std::unique_ptr<std::vector<std::pair<std::string, std::string>>> &manapi::net::http::response::replacers() MANAPIHTTP_NOEXCEPT {
    return this->replacers_;
}

manapi::error::status manapi::net::http::response::custom_data(custom_data_t data) MANAPIHTTP_NOEXCEPT {
    try {
        if (this->custom_data_) {
            custom_data_deleter_t b;
            b (this->custom_data_.get());
            *this->custom_data_ = std::move(data);
        }
        else
            this->custom_data_.reset(new custom_data_t (std::move(data)));

        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

manapi::net::http::config *manapi::net::http::response::config() MANAPIHTTP_NOEXCEPT {
    return this->config_;
}

manapi::net::http::custom_data_t *manapi::net::http::response::custom_data() MANAPIHTTP_NOEXCEPT {
    return this->custom_data_.get();
}

manapi::error::status_or<manapi::net::http::response::resp_callback_async*> manapi::net::http::response::callback_async() MANAPIHTTP_NOEXCEPT {
    auto res = this->check_type_(internal::RESPONSE_ASYNC_CALLBACK);
    if (!res)
        return std::move(res);
    return static_cast<resp_callback_async *> (this->data_);
}

manapi::error::status_or<manapi::net::http::response::resp_callback_sync *> manapi::net::http::response::callback_sync() MANAPIHTTP_NOEXCEPT {
    auto res = this->check_type_(internal::RESPONSE_SYNC_CALLBACK);
    if (!res)
        return std::move(res);
    return static_cast<resp_callback_sync *> (this->data_);
}

manapi::error::status_or<manapi::net::http::response::resp_stream *> manapi::net::http::response::callback_stream() MANAPIHTTP_NOEXCEPT {
    auto res = this->check_type_(internal::RESPONSE_STREAM);
    if (!res)
        return std::move(res);
    return static_cast<resp_stream *> (this->data_);
}

manapi::net::http::request_data_t * manapi::net::http::response::request_data() MANAPIHTTP_NOEXCEPT {
    return this->cdata_->req_data;
}

manapi::net::http::request * manapi::net::http::response::req() MANAPIHTTP_NOEXCEPT {
    assert(this->req_.get());
    return this->req_.get();
}

manapi::net::http::internal::handle_data_t * manapi::net::http::response::connection_data() MANAPIHTTP_NOEXCEPT {
    return this->cdata_;
}

manapi::net::http::internal::handle_data_t * manapi::net::http::response::connection_data_release() MANAPIHTTP_NOEXCEPT {
    return std::exchange(this->cdata_, nullptr);
}

manapi::error::status manapi::net::http::response::finish(std::unique_ptr<std::move_only_function<void(std::exception_ptr)>> cb) MANAPIHTTP_NOEXCEPT {
    if (this->finish_cb)
        return error::status_already_exists("finish callback already exists");
    this->finish_cb = std::move(cb);
    return error::status_ok();
}

void manapi::net::http::response::finish() MANAPIHTTP_NOEXCEPT {
    if (!this->finish_cb)
        return;

    try {
        auto cb = std::move(*this->finish_cb);
        this->finish_cb.reset();

        cb(nullptr);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "response finish failed", e.what());
    }
}

manapi::error::status manapi::net::http::response::check_type_(int type) MANAPIHTTP_NOEXCEPT {
    if (this->type_ != type || !this->data_)
        return error::status_invalid_argument("data is missing in response");
    return error::status_ok();
}

manapi::error::status manapi::net::http::response::replacers(std::vector<std::pair<std::string, std::string>> replacers) MANAPIHTTP_NOEXCEPT {
    try {
        this->replacers_ = std::make_unique<decltype(replacers)>(std::move(replacers));
        this->compress_enabled (false);
        this->partial_enabled (false);
        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

void manapi::net::http::response::partial_enabled(bool state) MANAPIHTTP_NOEXCEPT {
    if (state)
        this->flags |= internal::RESPONSE_FLAG_PARTITIAL_ENABLED;
    else if ((this->flags & internal::RESPONSE_FLAG_PARTITIAL_ENABLED))
        this->flags ^= internal::RESPONSE_FLAG_PARTITIAL_ENABLED;
}
#ifdef MANAPIHTTP_FETCH_SUPPORT
manapi::error::status manapi::net::http::response::proxy(std::string url) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<std::string>(std::move(url));
        this->type_ = internal::RESPONSE_PROXY;
        this->data_ = storage.release();

        this->compress_enabled(false);

        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

manapi::error::status manapi::net::http::response::proxy(std::string url, resp_proxy_setup_cb cb) MANAPIHTTP_NOEXCEPT {
    try {
        this->proxy(std::move(url));
        this->proxy_setup = std::make_unique<decltype(this->proxy_setup)::element_type>(std::move(cb));
        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}
#endif

manapi::error::status manapi::net::http::response::callback_sync(resp_callback_sync cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<resp_callback_sync>(std::move(cb));
        this->type_ = internal::RESPONSE_SYNC_CALLBACK;
        this->data_ = storage.release();
        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

manapi::error::status manapi::net::http::response::callback_async(resp_callback_async cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<resp_callback_async>(std::move(cb));
        this->type_ = internal::RESPONSE_ASYNC_CALLBACK;
        this->data_ = storage.release();
        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

manapi::error::status manapi::net::http::response::callback_stream(resp_stream cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<resp_stream>(std::move(cb));
        this->type_ = internal::RESPONSE_STREAM;
        this->data_ = storage.release();
        return error::status_ok();
    }
    catch (std::exception const &) {
        return error::status_resource_exhausted();
    }
}

manapi::error::status_or<std::string *> manapi::net::http::response::text() MANAPIHTTP_NOEXCEPT {
    auto err = this->check_type_(internal::RESPONSE_TEXT);
    if (!err)
        return std::move(err);
    return &this->body();
}

manapi::error::status_or<std::string *> manapi::net::http::response::url() MANAPIHTTP_NOEXCEPT {
    auto err = this->check_type_(internal::RESPONSE_PROXY);
    if (!err)
        return std::move(err);
    return &this->body();
}

std::unique_ptr<std::vector<std::pair<ssize_t, ssize_t>>> manapi::net::http::response::ranges() MANAPIHTTP_NOEXCEPT {
    return std::move(this->ranges_);
}

manapi::error::status_or<manapi::net::formdata_send *> manapi::net::http::response::formdata() MANAPIHTTP_NOEXCEPT {
    auto err = this->check_type_(internal::RESPONSE_FORMDATA);
    if (!err)
        return std::move(err);
    return static_cast<formdata_send *> (this->data_);
}

#ifdef MANAPIHTTP_FETCH_SUPPORT
std::unique_ptr<manapi::net::http::response::resp_proxy_setup_cb> &manapi::net::http::response::proxy_setup_cb() MANAPIHTTP_NOEXCEPT {
    return this->proxy_setup;
}
#endif
