#include <format>
#include <fstream>
#include <utility>

#include "ManapiAsync.hpp"
#include "ManapiFetch.hpp"
#include "http/ManapiHttpTypes.hpp"
#include "http/ManapiHttpMime.hpp"
#include "http/ManapiHttpResponse.hpp"
#include "http/ManapiHttpRequest.hpp"
#include "http/ManapiBaseHttp.hpp"
#include "../include/ManapiHttpStructs.hpp"
#include "../include/ManapiUtils.hpp"
#include "../include/ManapiHttpInternal.hpp"

static void free_response_data (uint8_t m_type, void *m_data) MANAPIHTTP_NOEXCEPT {
    switch (m_type) {
        case manapi::net::http::internal::RESPONSE_FORMDATA:
            delete static_cast<manapi::net::formdata_send *> (m_data);
        break;
        case manapi::net::http::internal::RESPONSE_SYNC_CALLBACK:
            delete static_cast<manapi::net::http::response::resp_callback_sync *> (m_data);
        break;
        case manapi::net::http::internal::RESPONSE_ASYNC_CALLBACK:
            delete static_cast<manapi::net::http::response::resp_callback_async *> (m_data);
        break;
        case manapi::net::http::internal::RESPONSE_STREAM:
            delete static_cast<manapi::net::http::response::resp_stream *> (m_data);
        break;
        case manapi::net::http::internal::RESPONSE_FILE:
        case manapi::net::http::internal::RESPONSE_PROXY:
        case manapi::net::http::internal::RESPONSE_TEXT:
            delete static_cast<std::string *> (m_data);
        break;
        default:
            break;
    }
}

void manapi::net::http::custom_data_deleter_t::operator()(custom_data_t *n) {
    try {
        if (n && n->clean)
            n->clean(n->src);

        delete n;
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "custom_data_deleter_t", e.what());
    }
}

manapi::net::http::response::response(internal::handle_data_t *cdata, uint16_t status, http::config *config, std::unique_ptr<http::request> req):
    m_req(std::move(req)), m_config(config), m_status_code(status) {
    this->m_cdata = cdata;
    this->m_type = internal::RESPONSE_NO_DATA;
    this->m_flags = 0;
    this->m_data = nullptr;
    this->m_compress = nullptr;

    /* TODO: remove */
    this->detect_ranges ();
}

manapi::net::http::response::~response() {
    free_response_data (this->m_type, this->m_data);

    delete this->m_cdata;
}


manapi::status manapi::net::http::response::header(const std::string &key, std::string value) MANAPIHTTP_NOEXCEPT {
    try {
        this->m_headers.insert_or_assign(key, std::move(value));
        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

void manapi::net::http::response::remove_header(std::string_view key) MANAPIHTTP_NOEXCEPT {
    auto const it = this->m_headers.find(key);
    if (it != this->m_headers.end())
        this->m_headers.erase(it);
}

bool manapi::net::http::response::contains_header(std::string_view key) const MANAPIHTTP_NOEXCEPT {
    return this->m_headers.find(key) != this->m_headers.end();
}

/**
 * if key exists returning the pointer to the string otherwise returning nullptr
 * @param key the key of the header
 * @return the pointer to the string | nullptr
 */
manapi::status_or<std::string_view> manapi::net::http::response::header(std::string_view key) MANAPIHTTP_NOEXCEPT {
    auto it = this->m_headers.find(key);
    if (it == this->m_headers.end())
        return status_not_found("headers:Not found");

    return std::string_view{it->second};
}

manapi::status manapi::net::http::response::text(std::string plain_text) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<std::string>(std::move(plain_text));
        free_response_data(this->m_type, this->m_data);
        this->m_type = internal::RESPONSE_TEXT;
        this->m_data = storage.release();
        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

manapi::status manapi::net::http::response::json(manapi::json data, size_t spaces) MANAPIHTTP_NOEXCEPT {
    try {
        auto res = header(std::string{H_CONTENT_TYPE}, std::string{manapi::mime::types.APPLICATION_JSON});
        if (!res)
            return std::move(res);
        return text(std::move(data.dump (static_cast<uint32_t>(spaces))));
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

manapi::status manapi::net::http::response::form(formdata_send formdata) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<formdata_send>(std::move(formdata));
        free_response_data(this->m_type, this->m_data);

        this->m_data = storage.release();
        this->m_type = internal::RESPONSE_FORMDATA;
        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

void manapi::net::http::response::status_code(uint16_t status_code) MANAPIHTTP_NOEXCEPT {
    this->m_status_code = status_code;
}

void manapi::net::http::response::status(uint16_t _status_code) MANAPIHTTP_NOEXCEPT {
    this->m_status_code = _status_code;
}

manapi::status manapi::net::http::response::file(std::string path) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<std::string>(std::move(path));
        free_response_data(this->m_type, this->m_data);

        this->m_type = internal::RESPONSE_FILE;
        this->m_data = storage.release();
        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

bool manapi::net::http::response::is_file() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == internal::RESPONSE_FILE;
}

bool manapi::net::http::response::is_text() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == internal::RESPONSE_TEXT;
}

bool manapi::net::http::response::is_proxy() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == internal::RESPONSE_PROXY;
}

bool manapi::net::http::response::is_no_data() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == internal::RESPONSE_NO_DATA;
}

bool manapi::net::http::response::is_formdata() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == internal::RESPONSE_FORMDATA;
}

bool manapi::net::http::response::is_async_cb() const MANAPIHTTP_NOEXCEPT {
    return this->m_type == internal::RESPONSE_ASYNC_CALLBACK;
}

bool manapi::net::http::response::is_sync_cb() const  MANAPIHTTP_NOEXCEPT{
    return this->m_type == internal::RESPONSE_SYNC_CALLBACK;
}

bool manapi::net::http::response::has_ranges() const MANAPIHTTP_NOEXCEPT {
    return this->m_ranges && !this->m_ranges->empty();
}

manapi::status_or<std::string *> manapi::net::http::response::file() MANAPIHTTP_NOEXCEPT {
    auto err = this->check_type_(internal::RESPONSE_FILE);
    if (!err.ok())
        return std::move(err);
    return &this->body();
}

uint16_t manapi::net::http::response::status_code() const MANAPIHTTP_NOEXCEPT {
    return this->m_status_code;
}

std::string_view manapi::net::http::response::status_message() MANAPIHTTP_NOEXCEPT {
    auto res = status_to_string(this->m_status_code);
    if (res)
        return res.unwrap();
    return {};
}

std::map<std::string, std::string, std::less<>> &manapi::net::http::response::headers() MANAPIHTTP_NOEXCEPT {
    return this->m_headers;
}

std::string &manapi::net::http::response::body() MANAPIHTTP_NOEXCEPT {
    return *static_cast<std::string *> (this->m_data);
}

manapi::status manapi::net::http::response::compress(std::string name) MANAPIHTTP_NOEXCEPT {
    try {
        this->m_compress = std::make_unique<std::string>(std::move(name));
        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

void manapi::net::http::response::compress_enabled (bool state) MANAPIHTTP_NOEXCEPT {
    if (state)
        this->m_flags |= internal::RESPONSE_FLAG_COMPRESS_ENABLED;
    else if (this->m_flags & internal::RESPONSE_FLAG_COMPRESS_ENABLED)
        this->m_flags ^= internal::RESPONSE_FLAG_COMPRESS_ENABLED;
}

std::string manapi::net::http::response::compress() MANAPIHTTP_NOEXCEPT {
    std::string compress;


    if ((this->m_flags & internal::RESPONSE_FLAG_COMPRESS_ENABLED)) {
        auto it = this->m_cdata->req_data->headers.find(H_ACCEPT_ENCODING);
        if (it != this->m_cdata->req_data->headers.end()) {

            if (this->m_compress) {
                compress = std::move(*this->m_compress);
            }

            if (it->second.size() < 1000) {
                /* lite check */

                std::string *last = nullptr;
                bool exists = false;
                auto rhs = http::parse_header_value(it->second);
                std::vector<header_value_t> data;
                if (rhs.ok()) {
                    data = rhs.unwrap();
                    for (auto &a: data) {
                        if (this->m_config->contains_compressor(a.value)) {
                            last = &a.value;
                            if (compress.empty()) {
                                break;
                            }
                            else {
                                if (a.value == compress) {
                                    exists = true;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (!exists) {
                    if (last) {
                        compress = std::move(*last);
                    }
                    else {
                        compress.clear();
                    }
                }
            }
            else {
                compress.clear();
            }
        }
    }

    return std::move(compress);
}

void manapi::net::http::response::detect_ranges () MANAPIHTTP_NOEXCEPT {
    if (this->m_ranges) {
        return;
    }

    auto it = this->m_cdata->req_data->headers.find(H_RANGE);
    if (it == this->m_cdata->req_data->headers.end()) {
        return;
    }

    auto rhs = http::parse_header_value(it->second);
    if (!rhs.ok())
        return;

    try {
        auto ranges = std::make_unique<decltype(this->m_ranges)::element_type>();

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

        if (!ranges->empty())
            this->m_ranges = std::move(ranges);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s failed due to %s", "resp:detect_ranges()", e.what());
    }
}

manapi::net::http::uresponse::uresponse(response *resp, std::unique_ptr<std::move_only_function<void(std::exception_ptr)>> cb) {
    assert (resp);
    resp->finish(std::move(cb));

    this->m_resp = resp;
}

manapi::net::http::uresponse::~uresponse() {
    if (this->m_resp) {
        this->m_resp->finish();
    }
}

manapi::net::http::uresponse::uresponse(uresponse &&n) MANAPIHTTP_NOEXCEPT {
    this->m_resp = n.m_resp;
    n.m_resp = nullptr;
}

manapi::net::http::uresponse & manapi::net::http::uresponse::operator=(uresponse &&n) MANAPIHTTP_NOEXCEPT {
    this->m_resp = n.m_resp;
    n.m_resp = nullptr;
    return *this;
}

manapi::net::http::response * manapi::net::http::uresponse::operator->() MANAPIHTTP_NOEXCEPT {
    return this->m_resp;
}

const manapi::net::http::response * manapi::net::http::uresponse::operator->() const MANAPIHTTP_NOEXCEPT {
    return this->m_resp;
}

manapi::net::http::response & manapi::net::http::uresponse::operator*() MANAPIHTTP_NOEXCEPT {
    return *this->m_resp;
}

const manapi::net::http::response & manapi::net::http::uresponse::operator*() const MANAPIHTTP_NOEXCEPT {
    return *this->m_resp;
}

void manapi::net::http::uresponse::finish() MANAPIHTTP_NOEXCEPT {
    if (this->m_resp) {
        this->m_resp->finish();
        this->m_resp = nullptr;
    }
}

bool manapi::net::http::response::partial_enabled() const MANAPIHTTP_NOEXCEPT {
    return (this->m_flags & internal::RESPONSE_FLAG_PARTITIAL_ENABLED);
}

int manapi::net::http::response::data_type() const MANAPIHTTP_NOEXCEPT {
    return this->m_type;
}

std::unique_ptr<std::vector<std::pair<std::string, std::string>>> &manapi::net::http::response::replacers() MANAPIHTTP_NOEXCEPT {
    return this->m_replacers;
}

manapi::status manapi::net::http::response::custom_data(custom_data_t data) MANAPIHTTP_NOEXCEPT {
    try {
        if (this->m_custom_data) {
            custom_data_deleter_t b;
            b (this->m_custom_data.get());
            *this->m_custom_data = std::move(data);
        }
        else
            this->m_custom_data.reset(new custom_data_t (std::move(data)));

        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

manapi::net::http::config *manapi::net::http::response::config() MANAPIHTTP_NOEXCEPT {
    return this->m_config;
}

manapi::net::http::custom_data_t *manapi::net::http::response::custom_data() MANAPIHTTP_NOEXCEPT {
    return this->m_custom_data.get();
}

manapi::status_or<manapi::net::http::response::resp_callback_async*> manapi::net::http::response::callback_async() MANAPIHTTP_NOEXCEPT {
    auto res = this->check_type_(internal::RESPONSE_ASYNC_CALLBACK);
    if (!res)
        return std::move(res);
    return static_cast<resp_callback_async *> (this->m_data);
}

manapi::status_or<manapi::net::http::response::resp_callback_sync *> manapi::net::http::response::callback_sync() MANAPIHTTP_NOEXCEPT {
    auto res = this->check_type_(internal::RESPONSE_SYNC_CALLBACK);
    if (!res)
        return std::move(res);
    return static_cast<resp_callback_sync *> (this->m_data);
}

manapi::status_or<manapi::net::http::response::resp_stream *> manapi::net::http::response::callback_stream() MANAPIHTTP_NOEXCEPT {
    auto res = this->check_type_(internal::RESPONSE_STREAM);
    if (!res)
        return std::move(res);
    return static_cast<resp_stream *> (this->m_data);
}

manapi::net::http::request_data_t * manapi::net::http::response::request_data() MANAPIHTTP_NOEXCEPT {
    return this->m_cdata->req_data;
}

manapi::net::http::request * manapi::net::http::response::req() MANAPIHTTP_NOEXCEPT {
    assert(this->m_req.get());
    return this->m_req.get();
}

manapi::net::http::internal::handle_data_t * manapi::net::http::response::connection_data() MANAPIHTTP_NOEXCEPT {
    return this->m_cdata;
}

manapi::net::http::internal::handle_data_t * manapi::net::http::response::connection_data_release() MANAPIHTTP_NOEXCEPT {
    return std::exchange(this->m_cdata, nullptr);
}

manapi::status manapi::net::http::response::finish(std::unique_ptr<std::move_only_function<void(std::exception_ptr)>> cb) MANAPIHTTP_NOEXCEPT {
    if (this->m_finish_cb)
        return status_already_exists("finish callback already exists");
    this->m_finish_cb = std::move(cb);
    return status_ok();
}

void manapi::net::http::response::finish() MANAPIHTTP_NOEXCEPT {
    if (!this->m_finish_cb)
        return;

    try {
        auto cb = std::move(*this->m_finish_cb);
        this->m_finish_cb.reset();

        cb(nullptr);
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "response finish failed", e.what());
    }
}

manapi::status manapi::net::http::response::check_type_(int type) MANAPIHTTP_NOEXCEPT {
    if (this->m_type != type || !this->m_data)
        return status_invalid_argument("data is missing in response");
    return status_ok();
}

manapi::status manapi::net::http::response::replacers(std::vector<std::pair<std::string, std::string>> replacers) MANAPIHTTP_NOEXCEPT {
    try {
        this->m_replacers = std::make_unique<decltype(replacers)>(std::move(replacers));
        this->compress_enabled (false);
        this->partial_enabled (false);
        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

void manapi::net::http::response::partial_enabled(bool state) MANAPIHTTP_NOEXCEPT {
    if (state)
        this->m_flags |= internal::RESPONSE_FLAG_PARTITIAL_ENABLED;
    else if ((this->m_flags & internal::RESPONSE_FLAG_PARTITIAL_ENABLED))
        this->m_flags ^= internal::RESPONSE_FLAG_PARTITIAL_ENABLED;
}
#ifdef MANAPIHTTP_FETCH_SUPPORT
manapi::status manapi::net::http::response::proxy(std::string url) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<std::string>(std::move(url));
        this->m_type = internal::RESPONSE_PROXY;
        this->m_data = storage.release();

        this->compress_enabled(false);

        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

manapi::status manapi::net::http::response::proxy(std::string url, resp_proxy_setup_cb cb) MANAPIHTTP_NOEXCEPT {
    try {
        this->proxy(std::move(url));
        this->m_proxy_setup = std::make_unique<decltype(this->m_proxy_setup)::element_type>(std::move(cb));
        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}
#endif

manapi::status manapi::net::http::response::callback_sync(resp_callback_sync cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<resp_callback_sync>(std::move(cb));
        this->m_type = internal::RESPONSE_SYNC_CALLBACK;
        this->m_data = storage.release();
        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

manapi::status manapi::net::http::response::callback_async(resp_callback_async cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<resp_callback_async>(std::move(cb));
        this->m_type = internal::RESPONSE_ASYNC_CALLBACK;
        this->m_data = storage.release();
        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

manapi::status manapi::net::http::response::callback_stream(resp_stream cb) MANAPIHTTP_NOEXCEPT {
    try {
        auto storage = std::make_unique<resp_stream>(std::move(cb));
        this->m_type = internal::RESPONSE_STREAM;
        this->m_data = storage.release();
        return status_ok();
    }
    catch (std::exception const &) {
        return status_resource_exhausted();
    }
}

manapi::status_or<std::string *> manapi::net::http::response::text() MANAPIHTTP_NOEXCEPT {
    auto err = this->check_type_(internal::RESPONSE_TEXT);
    if (!err)
        return std::move(err);
    return &this->body();
}

manapi::status_or<std::string *> manapi::net::http::response::url() MANAPIHTTP_NOEXCEPT {
    auto err = this->check_type_(internal::RESPONSE_PROXY);
    if (!err)
        return std::move(err);
    return &this->body();
}

bool manapi::net::http::response::contains_ranges() const MANAPIHTTP_NOEXCEPT {
    return !!this->m_ranges && !this->m_ranges->empty();
}

std::unique_ptr<std::vector<std::pair<ssize_t, ssize_t>>> manapi::net::http::response::ranges() MANAPIHTTP_NOEXCEPT {
    return std::move(this->m_ranges);
}

manapi::status_or<manapi::net::formdata_send *> manapi::net::http::response::formdata() MANAPIHTTP_NOEXCEPT {
    auto err = this->check_type_(internal::RESPONSE_FORMDATA);
    if (!err)
        return std::move(err);
    return static_cast<formdata_send *> (this->m_data);
}

#ifdef MANAPIHTTP_FETCH_SUPPORT
std::unique_ptr<manapi::net::http::response::resp_proxy_setup_cb> &manapi::net::http::response::proxy_setup_cb() MANAPIHTTP_NOEXCEPT {
    return this->m_proxy_setup;
}
#endif
