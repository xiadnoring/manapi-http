#include <format>
#include <fstream>
#include <memory.h>

#include "ManapiHttpRequest.hpp"
#include "ManapiJsonBuilder.hpp"
#include "ManapiHttpMime.hpp"
#include "http/base_http.hpp"


manapi::net::http_request::http_request(const manapi::net::http::manapi_socket_information &ip_data, manapi::net::http::request_data_t &request_data, http::base *http_task, std::shared_ptr<http::config> config, const void *handler) : config(std::move(config))
{
    this->ip_data = &ip_data;
    this->request_data = &request_data;
    this->http_task = http_task;
    this->page_handler = handler;
}

manapi::net::http_request::~http_request() = default;

const manapi::net::http::manapi_socket_information &manapi::net::http_request::get_ip_data() const {
    return *ip_data;
}

const std::string &manapi::net::http_request::get_method() const {
    return request_data->method;
}

const std::string &manapi::net::http_request::get_http_version() const {
    return request_data->http;
}

[[nodiscard]] const std::map<std::string, std::string> &manapi::net::http_request::ref_headers () const {
    return this->request_data->headers;
}

std::map<std::string, std::string> manapi::net::http_request::get_headers() const {
    return std::move(this->request_data->headers);
}

const std::string &manapi::net::http_request::get_param(const std::string &param) const {
    if (request_data->params.contains(param))
        return request_data->params.at(param);

    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PARAM_MISSING, "cannot find param '{}'", param);
}

std::string manapi::net::http_request::dump() const {
    std::string result;

    result += "HTTP: " + get_http_version() + '\n';
    result += "Method: " + get_method() + '\n';
    result += "URL: " + request_data->uri + '\n';

    result += "Headers: \n";

    for (const auto &header: ref_headers()) {
        result += http::stringify_header(header) + '\n';
    }

    result += '\n';

    result += "body\n";

    return result;
}

manapi::future<std::string> manapi::net::http_request::text() {
    if (!this->request_data->has_body)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MISSING, "{}", "this method cannot have a body");
    }

    std::string body;

    if (this->request_data->body_size > max_plain_body_size)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_TOO_LONG, "plain body can have only {} length", max_plain_body_size);
    }

    body.resize(this->request_data->body_size);

    size_t j                    = 0;
    //size_t socket_block_size    = http_server->get_socket_block_size();

    co_await _read_body([&body, &j] (const char *data, ssize_t size) -> void {
        memcpy (body.data() + j, data, size);
        j += size;
    });

    co_return body;
}

manapi::future<manapi::json> manapi::net::http_request::json()
{
    // TODO: check with json_mask during processing read_mask()
    const auto &post_mask = get_post_mask();
    json_builder builder (post_mask ? *post_mask : nullptr);
    co_await _read_body([&builder] (const char *data, ssize_t size) -> void {
        builder << std::string_view (data, size);
    });

    co_return std::move(builder.get());
}

manapi::future<manapi::net::formdata_recv> manapi::net::http_request::form ()
{
    formdata_recv formdata (this->http_task->get_site().async_context(), this->request_data->buffer->size(),
        this->request_data->body_part, this->request_data->buffer->data(), this->request_data->body_left, this->request_data->body_index, [http_base = this->http_task] (void *buff, ssize_t buff_size)
        -> future<ssize_t> { return http_base->read(buff, buff_size);  });
    co_await formdata._init(this->request_data->has_body, this->request_data->headers[HTTP_HEADER.CONTENT_TYPE]);
    co_return std::move(formdata);
}

manapi::future<> manapi::net::http_request::file(std::string filepath) {
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

ssize_t manapi::net::http_request::get_body_size() {
    return this->request_data->body_size;
}

void manapi::net::http_request::set_max_plain_body_size(const size_t &size) {
    max_plain_body_size = size;
}

bool manapi::net::http_request::contains_header(const std::string &name) {
    return request_data->headers.contains(name);
}

const std::string &manapi::net::http_request::get_header(const std::string &name) {
    return request_data->headers.at(name);
}

void manapi::net::http_request::parse_map_url_param() {
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

const std::string &manapi::net::http_request::get_query_param(const std::string &name) {
    if (map_url_params == nullptr)
    {
        parse_map_url_param();
    }

    if (map_url_params->contains(name))
    {
        return map_url_params->at(name);
    }

    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_QUERY_PARAM_MISSING, "Can not find query param by name: {}", name);
}

const std::unique_ptr<const manapi::json_mask> &manapi::net::http_request::get_post_mask() const {
    return static_cast<const http_handler_page *> (page_handler)->handler->post_mask;
}

const std::unique_ptr<const manapi::json_mask> &manapi::net::http_request::get_get_mask() const {
    return static_cast<const http_handler_page *> (page_handler)->handler->get_mask;
}

void manapi::net::http_request::stop_propagation(const bool &stop_propagation) {
    is_propagation = !stop_propagation;
}

bool manapi::net::http_request::get_propagation() const {
    return this->is_propagation;
}

manapi::future<void> manapi::net::http_request::_read_body(std::function<void(const char *, ssize_t)> handler) {
    this->request_data->body_part = std::min (this->request_data->body_part, this->request_data->body_left);

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

manapi::future<> manapi::net::http_request::_read_async_body(std::function<manapi::future<>(const char *, ssize_t)> handler) {
    this->request_data->body_part = std::min (this->request_data->body_part, this->request_data->body_left);

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
