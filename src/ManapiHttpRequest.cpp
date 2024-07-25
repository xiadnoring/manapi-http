#include <format>
#include <fstream>
#include <memory.h>
#include "ManapiTaskHttp.h"
#include "ManapiHttpRequest.h"

const std::string SPECIAL_SYMBOLS_BOUNDARY = "\r\n--";

manapi::net::http_request::http_request(manapi::net::ip_data_t &_ip_data, manapi::net::request_data_t &_request_data, void* _http_task, http* _http_server)
{
    ip_data         = &_ip_data;
    request_data    = &_request_data;
    http_task       = _http_task;
    http_server     = _http_server;
}

manapi::net::http_request::~http_request() {}

const manapi::net::ip_data_t &manapi::net::http_request::get_ip_data() {
    return *ip_data;
}

const std::string &manapi::net::http_request::get_method() {
    return request_data->method;
}

const std::string &manapi::net::http_request::get_http_version() {
    return request_data->http;
}

const std::map<std::string, std::string> &manapi::net::http_request::get_headers() {
    return request_data->headers;
}

const std::string &manapi::net::http_request::get_param(const std::string &param) {
    if (request_data->params.contains(param))
        return request_data->params.at(param);

    throw utils::manapi_exception (std::format("cannot find param '{}'", param));
}

std::string manapi::net::http_request::text() {
    if (!request_data->has_body)
        throw manapi::utils::manapi_exception ("this method cannot have a body");

    std::string body;

    if (request_data->body_size > max_plain_body_size)
        throw manapi::utils::manapi_exception (std::format("plain body can have only {} length", max_plain_body_size));

    body.resize(request_data->body_size);

    size_t j                    = 0;
    //size_t socket_block_size    = http_server->get_socket_block_size();

    request_data->body_part = std::min (request_data->body_part, request_data->body_left);

    // TODO: speed up
    for (; request_data->body_index < request_data->body_left; ++request_data->body_index, ++j) {
        if (request_data->body_index >= request_data->body_part) {
            request_data->body_part = manapi::net::http_task::read_next_part (request_data->body_left, request_data->body_index, http_task, request_data);
            if (request_data->body_part == 0)
            {
                break;
            }

            request_data->body_part       = std::min (request_data->body_part, request_data->body_left);
        }

        body[j] = request_data->body_ptr[request_data->body_index];
    }

    return body;
}

manapi::utils::json manapi::net::http_request::json()
{
    return manapi::utils::json (this->text(), true);
}

manapi::utils::MAP_STR_STR manapi::net::http_request::form ()
{
    if (!request_data->has_body)
    {
        throw manapi::utils::manapi_exception ("this method cannot have a body");
    }

    const auto header     = utils::parse_header_value(get_headers().at(http_header.CONTENT_TYPE));

    if (header.empty())
    {
        throw manapi::utils::manapi_exception ("value is empty");
    }

    const std::string *content_type = &header[0].value;

    manapi::utils::MAP_STR_STR params;

    if (*content_type == http_mime.MULTIPART_FORM_DATA)
    {
        if (!header[0].params.contains("boundary"))
        {
            throw manapi::utils::manapi_exception ("boundary not found");
        }

        body_boundary = SPECIAL_SYMBOLS_BOUNDARY + header[0].params.at("boundary");

        // the pointer to value of the param in a map params
        std::string *value_ptr;
        bool        next = true;

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
    else if (*content_type == http_mime.APPLICATION_X_WWW_FORM_URLENCODED)
    {
        request_data->body_part = std::min (request_data->body_part, request_data->body_left);

        std::string key, value;

        std::string *current = &key;

        char    buff_extra[2];
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
                    const char c = (char) (manapi::utils::hex2dec(buff_extra[0]) << 4 | manapi::utils::hex2dec(buff_extra[1]));

                    if (manapi::utils::valid_special_symbol(c)) {
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
        throw manapi::utils::manapi_exception ("Invalid MIME type");
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
        throw manapi::utils::manapi_exception ("no file in the body of the request");
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
    // size_t socket_block_size = http_server->get_socket_block_size();

    // TODO: move a extra buff outside the function

    // extra buffer
    size_t  max_size_extra  = std::max(4096UL, request_data->body_part);

    std::unique_ptr<char, void(*)(const char *)> buff_extra (
            (char *) malloc(max_size_extra),
            [] (const char *ptr){ delete ptr; }
    );

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
                    buff_to_extra_buff(request_data, checkpoint, request_data->body_index, buff_extra.get(), size_extra);
                }

                else
                {

                    if (size_extra > 0)
                    {
                        send_line(buff_extra.get(), size_extra);

                        size_extra = 0;
                    }

                    send_line(request_data->body_ptr + sizeof(char) * checkpoint,
                              request_data->body_index - checkpoint);
                }
            }

            else
            {
                // dont +1 bcz body_index == socket block size
                buff_to_extra_buff (request_data, checkpoint, request_data->body_index, buff_extra.get(), size_extra);
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

            if (--boundary_index > 2)
            {
                // TODO: RESOLVE CHECKER --\r\n form data
//                char *a = request_data->body_ptr + request_data->body_index * sizeof (char);
//                if (request_data->body_ptr[request_data->body_index] == '-')
//                {
//                    if (boundary_index == 2)
//                    {
//                        request_data->body_index ++;
//                        return;
//                    }
//
//                    continue;
//                }

                boundary_index -= 1;
            }

            // calc --{BOUNDARY}

            if (boundary_index == 0)
            {
                is_boundary = false;

                if (value)
                {
                    if (size_extra > 0)
                    {
                        buff_to_extra_buff (request_data, checkpoint, request_data->body_index, buff_extra.get(), size_extra);
                        const size_t result_size = size_extra - body_boundary.size() - 2;

                        send_line (buff_extra.get(), result_size);
                    }
                    else
                    {
                        char *a = request_data->body_ptr + request_data->body_index * sizeof (char);
                        size_t size_str = request_data->body_index - checkpoint - body_boundary.size() - 2;

                        send_line (request_data->body_ptr + sizeof (char) * checkpoint, size_str);
                    }

                    break;
                }

                checkpoint  = request_data->body_index;
                size_extra  = 0;
            }

            continue;
        }

        repeat:

        if (new_line)
        {
            new_line = false;

            size_t size_str = request_data->body_index + size_extra - checkpoint - 2 ;

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
                throw std::runtime_error ("Size of the extra buffer eq -1. Maybe the recv data was not provided?");
            }

            chars2string(line, buff_extra.get(), size_extra - (first + second));

            chars2string(line, request_data->body_ptr + sizeof (char) * checkpoint, request_data->body_index - checkpoint + first + second - 2);

            const auto parsed_header = utils::parse_header(line);
            const auto header_value = utils::parse_header_value(parsed_header.second);

            if (parsed_header.first == http_header.CONTENT_DISPOSITION)
            {
                if (header_value.empty())
                {
                    throw manapi::utils::manapi_exception ("value is empty");
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
                        throw manapi::utils::manapi_exception(
                                "the simple param can not be after the files in the body of the request");
                    }
                }
            }

            else if (parsed_header.first == http_header.CONTENT_TYPE)
            {
                if (header_value.empty())
                {
                    throw manapi::utils::manapi_exception("Content-Type cannot be empty");
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
                boundary_index  = 4;
                is_boundary     = true;

                if (file_data.exists)
                    file_data.exists    = false;
            }

            continue;
        }
        else if (boundary_index != 0)
        {
            // first \r\n is equal
            if (!value && boundary_index == 2)
            {
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
        throw manapi::utils::manapi_exception ("no file in the body of the request");
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
        throw manapi::utils::manapi_exception ("Cannot open a file to write");
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
        str[i] = ptr[j];
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
        map_url_params = new std::map<std::string, std::string>();
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

    throw manapi::utils::manapi_exception (std::format("Can not find query param by name \"{}\"", manapi::utils::escape_string(name)));
}
