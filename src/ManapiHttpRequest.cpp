#include <format>
#include <fstream>
#include <memory.h>

#include "ManapiHttpRequest.hpp"
#include "ManapiJsonBuilder.hpp"
#include "ManapiHttpMime.hpp"
#include "http/base_http.hpp"
#include "http/ManapiURLParams.hpp"


manapi::net::http::request::request(const manapi::net::http::manapi_socket_information &ip_data, manapi::net::http::request_data_t &request_data, http::base *http_task, std::shared_ptr<http::config> config, const void *handler) : config(std::move(config))
{
    this->ip_data_ = &ip_data;
    this->request_data = &request_data;
    this->http_task = http_task;
    this->page_handler = handler;
}

manapi::net::http::request::~request () = default;

const manapi::net::http::manapi_socket_information &manapi::net::http::request::ip_data() const {
    return *this->ip_data_;
}

const std::string &manapi::net::http::request::method() const {
    return this->request_data->method;
}

const std::string &manapi::net::http::request::http_version() const {
    return this->request_data->http;
}

[[nodiscard]] const std::map<std::string, std::string> &manapi::net::http::request::ref_headers () const {
    return this->request_data->headers;
}

std::map<std::string, std::string> manapi::net::http::request::headers() const {
    return std::move(this->request_data->headers);
}

const std::string &manapi::net::http::request::param(const std::string &param) const {
    if (this->request_data->params.contains(param))
        return this->request_data->params.at(param);

    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PARAM_MISSING, "cannot find param '{}'", param);
}

std::string manapi::net::http::request::dump() const {
    std::string result;

    result += "HTTP: " + this->http_version() + '\n';
    result += "Method: " + this->method() + '\n';
    result += "URL: " + this->request_data->uri + '\n';

    result += "Headers: \n";

    for (const auto &header: ref_headers()) {
        result += http::stringify_header(header) + '\n';
    }

    result += '\n';

    result += "body\n";

    return result;
}

manapi::future<std::string> manapi::net::http::request::text() {
    if (!this->request_data->has_body)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MISSING, "{}", "this method cannot have a body");
    }

    std::string body;

    if (this->request_data->body_size > this->max_plain_body_size_)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_TOO_LONG, "plain body can have only {} length", max_plain_body_size_);
    }

    body.resize(this->request_data->body_size);

    size_t j = 0;
    //size_t socket_block_size    = http_server->get_socket_block_size();

    co_await _read_body([&body, &j] (const char *data, ssize_t size) -> void {
        memcpy (body.data() + j, data, size);
        j += size;
    });

    co_return body;
}

manapi::future<manapi::json> manapi::net::http::request::json()
{
    // TODO: check with json_mask during processing read_mask()
    const auto &post_mask = this->post_mask();

    json_builder builder = post_mask ? json_builder (*post_mask) : json_builder ();
    co_await _read_body([&builder] (const char *data, ssize_t size) -> void {
        builder << std::string_view (data, size);
    });

    co_return std::move(builder.get());
}

manapi::future<manapi::net::formdata_recv> manapi::net::http::request::form ()
{
    formdata_recv formdata (this->http_task->get_site().async_context(), this->request_data->buffer->size(),
        this->request_data->body_part, this->request_data->buffer->data(), this->request_data->body_left, this->request_data->body_index, [http_base = this->http_task] (void *buff, ssize_t buff_size)
        -> future<ssize_t> { return http_base->read(buff, buff_size);  });
    co_await formdata._init(this->request_data->has_body, this->request_data->headers[http::HEADER.CONTENT_TYPE]);
    co_return std::move(formdata);
}

manapi::future<> manapi::net::http::request::file(std::string filepath) {
    manapi::filesystem::async::fstream f (this->http_task->get_site().async_context(), filepath);
    co_await f.open(f.FILE_WRITE|f.FILE_CREATE|f.FILE_TRUNC);

    if (!f.is_open()) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_FILE_IO, "http request: Failed to open the file ({}) to write", filepath);
    }

    std::exception_ptr err{nullptr};

    try {
        co_await this->_read_async_body([&] (const char *data, ssize_t size)
            -> manapi::future<> { return f.fwrite(data, size); });
    }
    catch (...) {
        err = std::current_exception();
    }

    co_await f.close();

    if (err) {
        std::rethrow_exception(std::move(err));
    }
}

ssize_t manapi::net::http::request::body_size() {
    return this->request_data->body_size;
}

const std::string & manapi::net::http::request::get(const std::string &key) {
    this->prepare_get_params_();

    auto it = this->get_params_.find(key);
    if (it == this->get_params_.end()) {
        THROW_MANAPIHTTP_EXCEPTION (ERR_HTTP_PARAM_MISSING, "GET param {} is missing", key);
    }

    return it->second;
}

bool manapi::net::http::request::contains_get(const std::string &key) {
    this->prepare_get_params_();
    return this->get_params_.contains(key);
}

void manapi::net::http::request::max_plain_body_size(const size_t &size) {
    this->max_plain_body_size_ = size;
}

bool manapi::net::http::request::contains_header(const std::string &name) {
    return this->request_data->headers.contains(name);
}

const std::string &manapi::net::http::request::header(const std::string &name) {
    return this->request_data->headers.at(name);
}

void manapi::net::http::request::prepare_get_params_() {
    if (this->get_params_.empty() && this->request_data->divided != -1) {
        this->get_params_ = http::parse_get_params (this->request_data->path[this->request_data->divided]);
    }
}

void manapi::net::http::request::parse_map_url_param() {
    if (map_url_params == nullptr)
    {
        map_url_params = std::make_unique<std::map <std::string, std::string> >();
    }

    if (request_data->divided != -1)
    {
        for (size_t i = request_data->divided; i < request_data->path.size(); i++)
        {
            std::cerr << request_data->path[i] << "\n";
        }
    }
}

const std::string &manapi::net::http::request::query_param(const std::string &name) {
    if (this->map_url_params == nullptr)
    {
        parse_map_url_param();
    }

    if (this->map_url_params->contains(name))
    {
        return this->map_url_params->at(name);
    }

    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_QUERY_PARAM_MISSING, "Can not find query param by name: {}", name);
}

const std::unique_ptr<const manapi::json_mask> &manapi::net::http::request::post_mask() const {
    return static_cast<const http_handler_page *> (this->page_handler)->handler->post_mask;
}

const std::unique_ptr<const manapi::json_mask> &manapi::net::http::request::get_mask() const {
    return static_cast<const http_handler_page *> (this->page_handler)->handler->get_mask;
}

void manapi::net::http::request::stop_propagation(const bool &stop_propagation) {
    this->is_propagation = !stop_propagation;
}

bool manapi::net::http::request::propagation() const {
    return this->is_propagation;
}

manapi::future<void> manapi::net::http::request::_read_body(std::function<void(const char *, ssize_t)> handler) {
    this->request_data->body_part = std::min (this->request_data->body_part, this->request_data->body_left)
        - this->request_data->body_index;
    this->request_data->body_left -= this->request_data->body_index;

    while (true) {
        handler (this->request_data->buffer->data() + this->request_data->body_index, this->request_data->body_part);
        this->request_data->body_left -= this->request_data->body_part;
        this->request_data->body_index = 0;

        if (this->request_data->body_left > 0) {
            this->request_data->body_part = co_await this->http_task->read(this->request_data->buffer->data(), static_cast<ssize_t>(this->request_data->buffer->size()));
            if (this->request_data->body_part < 0) {
                THROW_MANAPIHTTP_EXCEPTION2 (ERR_HTTP_CONNECTION_WAS_CLOSED, "Connection was closed");
            }
            if (this->request_data->body_part == 0) {
                break;
            }

            continue;
        }

        break;
    }
}

manapi::future<> manapi::net::http::request::_read_async_body(std::function<manapi::future<>(const char *, ssize_t)> handler) {
    this->request_data->body_part = std::min (this->request_data->body_part, this->request_data->body_left)
        - this->request_data->body_index;
    this->request_data->body_left -= this->request_data->body_index;

    while (true) {
        co_await handler (this->request_data->buffer->data() + this->request_data->body_index, this->request_data->body_part);
        this->request_data->body_left -= this->request_data->body_part;
        this->request_data->body_index = 0;

        if (this->request_data->body_left > 0) {
            this->request_data->body_part = co_await this->http_task->read(this->request_data->buffer->data(), static_cast<ssize_t>(this->request_data->buffer->size()));
            if (this->request_data->body_part < 0) {
                THROW_MANAPIHTTP_EXCEPTION2 (ERR_HTTP_CONNECTION_WAS_CLOSED, "Connection was closed");
            }
            if (this->request_data->body_part == 0) {
                break;
            }

            continue;
        }

        break;
    }
}
