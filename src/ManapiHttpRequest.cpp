#include <format>
#include <fstream>
#include <memory.h>
#include "ManapiTaskHttp.hpp"
#include "ManapiHttpRequest.hpp"

#include "ManapiJsonBuilder.hpp"

const std::string SPECIAL_SYMBOLS_BOUNDARY = "\r\n--";

manapi::net::http_request::http_request(const manapi::net::utils::manapi_socket_information &_ip_data, manapi::net::request_data_t &_request_data, void* _http_task, class config *config, const void *_handler)
{
    this->config = config;

    ip_data         = &_ip_data;
    request_data    = &_request_data;
    http_task       = _http_task;
    page_handler    = _handler;

    buff_extra      = nullptr;
}

manapi::net::http_request::~http_request() {
    if (buff_extra != nullptr)
    {
        delete[] buff_extra;
    }
}

const manapi::net::utils::manapi_socket_information &manapi::net::http_request::get_ip_data() const {
    return *ip_data;
}

const std::string &manapi::net::http_request::get_method() const {
    return request_data->method;
}

const std::string &manapi::net::http_request::get_http_version() const {
    return request_data->http;
}

const std::map<std::string, std::string> &manapi::net::http_request::get_headers() const {
    return request_data->headers;
}

const std::string &manapi::net::http_request::get_param(const std::string &param) const {
    if (request_data->params.contains(param))
        return request_data->params.at(param);

    THROW_MANAPI_EXCEPTION(ERR_HTTP_PARAM_MISSING, "cannot find param '{}'", param);
}

std::string manapi::net::http_request::dump() const {
    std::string result;

    result += "HTTP: " + get_http_version() + '\n';
    result += "Method: " + get_method() + '\n';
    result += "URL: " + request_data->uri + '\n';

    result += "Headers: \n";

    for (const auto &header: get_headers()) {
        result += utils::stringify_header(header) + '\n';
    }

    result += '\n';

    result += "body\n";

    return result;
}

std::string manapi::net::http_request::text() {
    if (!request_data->has_body)
    {
        THROW_MANAPI_EXCEPTION(ERR_HTTP_BODY_MISSING, "{}", "this method cannot have a body");
    }

    std::string body;

    if (request_data->body_size > max_plain_body_size)
    {
        THROW_MANAPI_EXCEPTION(ERR_HTTP_BODY_SO_LONG, "plain body can have only {} length", max_plain_body_size);
    }

    body.resize(request_data->body_size);

    size_t j                    = 0;
    //size_t socket_block_size    = http_server->get_socket_block_size();

    _read_body([&body, &j] (const char *data, const size_t &size) -> void {
        memcpy (body.data() + j, data, size);
        j += size;
    });

    return body;
}

manapi::json manapi::net::http_request::json()
{
    // TODO: check with json_mask during processing read_mask()
    const auto &post_mask = get_post_mask();
    json_builder builder (*post_mask);
    _read_body([&builder] (const char *data, const size_t &size) -> void {
        builder << std::string_view (data, size);
    });

    return std::move(builder.get());
}

manapi::net::utils::MAP_STR_STR manapi::net::http_request::form ()
{
    if (!request_data->has_body)
    {
        THROW_MANAPI_EXCEPTION(ERR_HTTP_BODY_MISSING, "{}", "this method cannot have a body");
    }

    const auto header     = utils::parse_header_value(get_headers().at(HTTP_HEADER.CONTENT_TYPE));

    if (header.empty())
    {
        THROW_MANAPI_EXCEPTION (ERR_HTTP_CONTENT_TYPE_MISSING, "header value is empty: {}", HTTP_HEADER.CONTENT_TYPE);
    }

    const std::string *content_type = &header[0].value;

    manapi::net::utils::MAP_STR_STR params;

    if (*content_type == HTTP_MIME.MULTIPART_FORM_DATA)
    {
        if (!header[0].params.contains("boundary"))
        {
            THROW_MANAPI_EXCEPTION(ERR_HTTP_BODY_BOUNDARY_MISSING, "{}", "boundary not found");
        }

        body_boundary = SPECIAL_SYMBOLS_BOUNDARY + header[0].params.at("boundary");

        // the pointer to value of the param in a map params
        std::string *value_ptr;
        bool        next = true;

        buff_extra = new char[std::max(4096UL,  config->get_socket_block_size() * 4)];

        while (next)
        {
            next = false;

            this->multipart_read_param([&](const char *buff, const size_t &si) {
                chars2string (*value_ptr, buff, si);
            },[&params, &value_ptr, &next](const std::string &name) {
                params.insert({name, ""});

                value_ptr   = &params[name];
                next        = true;
            });
        }
    }
    else if (*content_type == HTTP_MIME.APPLICATION_X_WWW_FORM_URLENCODED)
    {
        request_data->body_part = std::min (request_data->body_part, request_data->body_left);

        std::string key, value;

        std::string *current = &key;

        buff_extra = new char[2];

        size_t  size_extra  = 0;
        bool    used_extra  = false;

        for (; request_data->body_index < request_data->body_left; request_data->body_index++) {
            if (request_data->body_index >= request_data->body_part) {
                // get the next data
                request_data->body_part = manapi::net::http_task::read_next_part (request_data->body_left, request_data->body_index, http_task, request_data);
                if (request_data->body_part == 0)
                {
                    break;
                }

                request_data->body_part       = std::min (request_data->body_part, request_data->body_left);
            }

            if (used_extra) {
                buff_extra[size_extra] = request_data->body_ptr[request_data->body_index];
                size_extra++;

                if (size_extra == 2) {
                    const char c = (char) (manapi::net::utils::hex2dec(buff_extra[0]) << 4 | manapi::net::utils::hex2dec(buff_extra[1]));

                    if (manapi::net::utils::valid_special_symbol(c))
                    {
                        (*current)  += c;
                    }
                    else {
                        current->push_back('%');

                        *current += buff_extra[0];
                        *current += buff_extra[1];
                    }

                    size_extra  = 0;
                    used_extra  = false;
                }

                continue;
            }

            if (request_data->body_ptr[request_data->body_index] == '%') {
                used_extra = true;

                continue;
            }

            else if (request_data->body_ptr[request_data->body_index] == '=') {
                current = &value;

                continue;
            }

            else if (request_data->body_ptr[request_data->body_index] == '&') {
                params.insert({key, value});
                key     = "";
                value   = "";

                current = &key;

                continue;
            }

            else if (request_data->body_ptr[request_data->body_index] == '+') {
                *current += ' ';

                continue;
            }

            *current += request_data->body_ptr[request_data->body_index];
        }

        if (!value.empty() || !key.empty())
        {
            params.insert({key, value});
        }
    }
    else
    {
        THROW_MANAPI_EXCEPTION(ERR_HTTP_INVALID_CONTENT_TYPE, "Invalid POST DATA MIME-type: {}", utils::escape_string(*content_type));
    }

    // validate data
    const auto &post_mask = get_post_mask();
    if (post_mask != nullptr)
    {
        if (!post_mask->valid(params))
        {
            THROW_MANAPI_EXCEPTION(ERR_HTTP_BODY_MASK_FAILED, "Form Data: {} ({})", "Failed validation", utils::escape_string(*content_type));
        }
    }

    return params;
}

const size_t &manapi::net::http_request::get_body_size() {
    return request_data->body_size;
}

void manapi::net::http_request::set_max_plain_body_size(const size_t &size) {
    max_plain_body_size = size;
}

const manapi::net::file_data_t &manapi::net::http_request::inf_file() {
    if (!file_data.exists)
    {
        THROW_MANAPI_EXCEPTION(ERR_HTTP_BODY_NOT_CONTAINS_FILE, "{}", "No found any file in the body of the request");
    }

    return file_data;
}

bool manapi::net::http_request::has_file() const {
    return file_data.exists;
}

void manapi::net::http_request::buff_to_extra_buff(const request_data_t *req_data, const size_t &start, const size_t &end, char *dest, size_t &size) {
    const size_t size2copy = end - start;
    memcpy (dest + size, req_data->body_ptr + start, size2copy);
    size += size2copy;
}

void manapi::net::http_request::multipart_read_param (const std::function<void(const char *, const size_t &)> &send_line, const std::function<void(const std::string &)> &send_name) {
    size_t  size_extra      = 0;

    request_data->body_part = std::min(request_data->body_part, request_data->body_left);

    bool        value       = false;
    bool        new_line    = false;
    bool        is_boundary = false;

    std::string name;

    if (file_data.exists)
    {
        value               = true;
    }

    // the start of the string
    size_t checkpoint       = request_data->body_index;

    // boundary size which equal
    size_t boundary_index   = first_line ? 2 : 0;

    // if it is a first line in the body
    if (first_line)
    {
        first_line = false;
    }

    for (;request_data->body_index < request_data->body_left; request_data->body_index++)
    {
        if (request_data->body_index >= request_data->body_part)
        {
            // bcz the file can be massive, and not to must contains any delimiter (\r\n), we break it by block size
            if (file_data.exists && name.empty())
            {
                if (boundary_index > 0)
                {
                    buff_to_extra_buff(request_data, checkpoint, request_data->body_index, buff_extra, size_extra);
                }

                else
                {

                    if (size_extra > 0)
                    {
                        send_line(buff_extra, size_extra);

                        size_extra = 0;
                    }

                    send_line(request_data->body_ptr + sizeof(char) * checkpoint,
                              request_data->body_index - checkpoint);
                }
            }

            else
            {
                // dont +1 bcz body_index == socket block size
                buff_to_extra_buff (request_data, checkpoint, request_data->body_index, buff_extra, size_extra);
            }

            request_data->body_part = manapi::net::http_task::read_next_part (request_data->body_left, request_data->body_index, http_task, request_data);
            request_data->body_part = std::min(request_data->body_part, request_data->body_left);

            // read next block
            if (request_data->body_part == 0)
            {
                break;
            }

            checkpoint = 0;
        }

        if (is_boundary)
        {
            // calc --{BOUNDARY}--

            // calc --{BOUNDARY}



            if (boundary_index == 0)
            {
                is_boundary = false;

                if (value)
                {
                    if (size_extra > 0)
                    {
                        buff_to_extra_buff (request_data, checkpoint, request_data->body_index, buff_extra, size_extra);
                        const size_t result_size = size_extra - body_boundary.size() - 2;

                        send_line (buff_extra, result_size);
                    }
                    else
                    {
                        size_t size_str = request_data->body_index - checkpoint - body_boundary.size() - 2;

                        send_line (request_data->body_ptr + sizeof (char) * checkpoint, size_str);
                    }

                    break;
                }

                checkpoint  = request_data->body_index;
                size_extra  = 0;
                if (request_data->body_size - 2 == request_data->body_index) { request_data->body_index+=2; }
            }
            else
            {
                boundary_index--;
            }

            continue;
        }

        repeat:

        if (new_line)
        {
            new_line = false;


            size_t size_str = request_data->body_index + size_extra;
            if (size_str < checkpoint + 2) {
                THROW_MANAPI_EXCEPTION(ERR_FATAL, "BUG: size_str < {}.", 0);
            }
            size_str = size_str - checkpoint - 2;

            if (!name.empty() && size_str == 0 && !value)
            {
                if (file_data.exists)
                {
                    break;
                }


                value = true;

                checkpoint = request_data->body_index;
                size_extra = 0;

                goto repeat;
            }

            std::string line;

            // which buff contains \r or \n symbols ?
            const bool  first   = request_data->body_index < 2,
                        second  = request_data->body_index < 1;

            if (size_extra < first + second)
            {
                THROW_MANAPI_EXCEPTION(ERR_FATAL, "{}", "Size of the extra buffer eq -1. Maybe the recv data was not provided?");
            }

            chars2string(line, buff_extra, size_extra - (first + second));

            chars2string(line, request_data->body_ptr + sizeof (char) * checkpoint, request_data->body_index - checkpoint + first + second - 2);

            const auto parsed_header = utils::parse_header(line);
            const auto header_value = utils::parse_header_value(parsed_header.second);

            if (parsed_header.first == HTTP_HEADER.CONTENT_DISPOSITION)
            {
                if (header_value.empty())
                {
                    THROW_MANAPI_EXCEPTION(ERR_HTTP_IMPORTANT_HEADER_MISSING, "header value is empty: {}", HTTP_HEADER.CONTENT_DISPOSITION);
                }

                if (header_value[0].params.contains("name"))
                {
                    name        = header_value[0].params.at("name");
                }


                if (header_value[0].params.contains("filename"))
                {
                    file_data.exists        = true;

                    file_data.file_name     = header_value[0].params.at("filename");
                    file_data.param_name    = name;
                }

                else {
                    if (send_name != nullptr)
                    {
                        send_name (name);
                    }
                    else
                    {
                        THROW_MANAPI_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "{}", "the simple param can not be after the files in the body of the request");
                    }
                }
            }

            else if (parsed_header.first == HTTP_HEADER.CONTENT_TYPE)
            {
                if (header_value.empty())
                {
                    THROW_MANAPI_EXCEPTION(ERR_HTTP_IMPORTANT_HEADER_MISSING, "{} can not be empty", HTTP_HEADER.CONTENT_TYPE);
                }

                if (file_data.exists)
                {
                    file_data.mime_type     = header_value[0].value;
                }
            }

            checkpoint  = request_data->body_index;
            size_extra  = 0;
        }

        if (request_data->body_ptr[request_data->body_index] == body_boundary[boundary_index])
        {
            boundary_index ++;

            if (body_boundary.size() == boundary_index)
            {
                boundary_index  = 2;
                is_boundary     = true;

                if (file_data.exists)
                { file_data.exists    = false; }
            }

            continue;
        }
        if (boundary_index != 0)
        {
            // first \r\n is equal, but other is not equ -> cut
            if (!value && boundary_index >= 2)
            {
                boundary_index -= 2;
                // can back in buf
                const size_t can_back = std::min (request_data->body_index, boundary_index);
                const size_t to_buff = boundary_index - can_back;
                memcpy(buff_extra + size_extra, body_boundary.data() + 2, to_buff);
                size_extra += to_buff;
                request_data->body_index -= can_back;
                new_line            = true;
            }

            boundary_index = 0;

            goto repeat;

        }
    }
}

std::string manapi::net::http_request::set_file_to_str() {
    std::string content;

    set_file([&] (const char *ptr, const size_t &size) {
        chars2string (content, ptr, size);
    });

    return content;
}

void manapi::net::http_request::set_file(const std::function<void(const char *, const size_t &)> &handler) {
    if (!has_file())
    {
        THROW_MANAPI_EXCEPTION(ERR_HTTP_BODY_NOT_CONTAINS_FILE, "{}", "no file in the body of the request");
    }

    for (char i = 0; i < 2; i++)
    {
        if (i == 1)
        {
            file_data.exists = false;
        }

        multipart_read_param(handler);
    }
}

void manapi::net::http_request::set_file_to_local (const std::string &filepath) {
    std::ofstream out (filepath);

    if (!out.is_open())
    {
        THROW_MANAPI_EXCEPTION(ERR_FILE_IO, "Cannot open a file to write: {}", filepath);
    }

    set_file([&] (const char *ptr, const size_t & size) {
        out.write(ptr, (ssize_t) size);
    });

    out.close();
}

void manapi::net::http_request::chars2string(std::string &str, const char *ptr, const size_t &size) {
    const size_t total = str.size() + size;
    size_t i = str.size();
    size_t j = 0;

    str.resize(total);

    for (; i < total; i++, j++)
    {
        str[i] = ptr[j];
    }
}

bool manapi::net::http_request::contains_header(const std::string &name) {
    return request_data->headers.contains(name);
}

const std::string &manapi::net::http_request::get_header(const std::string &name) {
    return request_data->headers.at(name);
}

bool manapi::net::http_request::has_header(const std::string &name) {
    return request_data->headers.contains(name);
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

    THROW_MANAPI_EXCEPTION(ERR_HTTP_QUERY_PARAM_MISSING, "Can not find query param by name: {}", name);
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

const bool & manapi::net::http_request::get_propagation() {
    return is_propagation;
}

void manapi::net::http_request::_read_body(const std::function<void(const char *, const size_t &)> &handler) {
    request_data->body_part = std::min (request_data->body_part, request_data->body_left);

    // TODO: speed up
    while (request_data->body_index < request_data->body_left) {
        if (request_data->body_index >= request_data->body_part) {
            request_data->body_part = manapi::net::http_task::read_next_part (request_data->body_left, request_data->body_index, http_task, request_data);
            if (request_data->body_part == 0)
            {
                break;
            }

            request_data->body_part       = std::min (request_data->body_part, request_data->body_left);
        }


        ///body[j] = request_data->body_ptr[request_data->body_index];
        handler (request_data->body_ptr, request_data->body_part - request_data->body_index);
        request_data->body_index = request_data->body_part;
    }
}
