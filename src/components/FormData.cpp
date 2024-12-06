#include <memory.h>

#include "components/FormData.hpp"

#include "http/Base.hpp"
#include "ManapiHttpMime.hpp"

const std::string SPECIAL_SYMBOLS_BOUNDARY = "\r\n--";

manapi::net::formdata_recv::formdata_recv(request_data_t &request_data, std::shared_ptr<http::config> config, http::base *task) : http_task(task), request_data(request_data), config(std::move(config)) {
    if (!request_data.has_body)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MISSING, "{}", "this method cannot have a body");
    }

    const auto header     = utils::parse_header_value(request_data.headers.at(HTTP_HEADER.CONTENT_TYPE));

    if (header.empty())
    {
        THROW_MANAPIHTTP_EXCEPTION (ERR_HTTP_CONTENT_TYPE_MISSING, "header value is empty: {}", HTTP_HEADER.CONTENT_TYPE);
    }

    const std::string &content_type = header[0].value;

    if (content_type == HTTP_MIME.MULTIPART_FORM_DATA)
    {
        if (!header[0].params.contains("boundary"))
        {
            THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_BOUNDARY_MISSING, "{}", "boundary not found");
        }

        body_boundary = SPECIAL_SYMBOLS_BOUNDARY + header[0].params.at("boundary");

        // the pointer to value of the param in a map params
        buff_extra.resize(std::max(4096UL, request_data.buffer.size()));

        // get the first metadata (name, type and etc)
        current_read_param = [this] (auto &&param1) -> void { this->multipart_read_param (std::forward<decltype(param1)>(param1));  };
        current_read_param(nullptr);
    }
    else if (content_type == HTTP_MIME.APPLICATION_X_WWW_FORM_URLENCODED)
    {
        request_data.body_part = std::min (request_data.body_part, request_data.body_left);

        buff_extra.resize(2);

        current_read_param = [this] (auto &&param1) -> void { this->urlencoded_read_param (std::forward<decltype(param1)>(param1));  };
        current_read_param(nullptr);
    }
    else
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_INVALID_CONTENT_TYPE, "Invalid POST DATA MIME-type: {}", content_type);
    }

    // validate data
    // const auto &post_mask = get_post_mask();
    // if (post_mask != nullptr)
    // {
    //     if (!post_mask->valid(params))
    //     {
    //         THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MASK_FAILED, "Form Data: {} ({})", "Failed validation", utils::escape_string(*content_type));
    //     }
    // }
}

manapi::net::formdata_recv::~formdata_recv() = default;

bool manapi::net::formdata_recv::next_file() const {
    return type == DATA_FILE;
}

bool manapi::net::formdata_recv::next_param() const {
    return type == DATA_PLAIN;
}

void manapi::net::formdata_recv::buff_to_extra_buff(const request_data_t &req_data, const size_t &start, const size_t &end, std::string &dest, size_t &size) {
    const size_t size2copy = end - start;
    auto available = dest.size();
    if (size + size2copy > available) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "extra buffer overflow");
    }
    memcpy (dest.data() + size, req_data.body_ptr + start, size2copy);
    size += size2copy;
}

void manapi::net::formdata_recv::multipart_read_param (const std::function<void(const char *, const size_t &)> &send_line) {
    try {
        size_t  size_extra      = 0;

        request_data.body_part = std::min(request_data.body_part, request_data.body_left);

        bool        value = false;
        bool        new_line = false;
        bool        is_boundary = false;

        std::string name;

        if (type != DATA_NONE)
        {
            // we parse value for now
            value = true;
        }

        // the start of the string
        size_t checkpoint = request_data.body_index;

        // boundary size which equal
        size_t boundary_index = first_line ? 2 : 0;

        // if it is a first line in the body
        if (first_line)
        {
            first_line = false;
        }

        for (;request_data.body_index < request_data.body_left; request_data.body_index++)
        {
            if (request_data.body_index >= request_data.body_part)
            {

                if (size_extra > 0)
                {
                    send_line(buff_extra.data(), size_extra);

                    size_extra = 0;
                }

                // bcz the file can be massive, and not to must contains any delimiter (\r\n), we break it by block size
                if (type != DATA_NONE)
                {

                    if (boundary_index > 0)
                    {
                        buff_to_extra_buff(request_data, checkpoint, request_data.body_index, buff_extra, size_extra);
                    }

                    else
                    {

                        send_line(request_data.body_ptr + sizeof(char) * checkpoint,
                                  request_data.body_index - checkpoint);
                    }
                }

                else
                {
                    // dont +1 bcz body_index == socket block size
                    buff_to_extra_buff (request_data, checkpoint, request_data.body_index, buff_extra, size_extra);
                }

                request_data.body_left -= request_data.body_index;
                request_data.body_index =0;
                // get the next data
                ssize_t rhs = http_task->read (request_data.buffer.data(), request_data.buffer.size());
                if (rhs == -1) {
                    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "socket read error: read_next() = {}", rhs);
                }
                if (rhs > request_data.body_left) {
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_BODY_TOO_LONG, "http body too long. take it easy");
                }
                request_data.body_part = std::min(static_cast <size_t>(rhs), request_data.body_left);

                // read next block
                if (request_data.body_part == 0)
                {
                    break;
                }

                checkpoint = 0;
            }

            if (is_boundary)
            {
                // the boundary was expected
                // calc --{BOUNDARY}--

                // calc --{BOUNDARY}



                if (boundary_index == 0)
                {
                    is_boundary = false;

                    if (value)
                    {
                        if (size_extra > 0)
                        {
                            buff_to_extra_buff (request_data, checkpoint, request_data.body_index, buff_extra, size_extra);
                            const size_t result_size = size_extra - body_boundary.size() - 2;

                            send_line (buff_extra.data(), result_size);
                        }
                        else
                        {
                            size_t size_str = request_data.body_index - checkpoint - body_boundary.size() - 2;

                            send_line (request_data.body_ptr + sizeof (char) * checkpoint, size_str);
                        }

                        value = false;
                    }

                    checkpoint  = request_data.body_index;
                    size_extra  = 0;
                    if (request_data.body_size - 2 == request_data.body_index) { request_data.body_index+=2; }
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


                size_t size_str = request_data.body_index + size_extra;
                if (size_str < checkpoint + 2) {
                    THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "BUG: size_str < {}.", 0);
                }
                size_str = size_str - checkpoint - 2;

                if (size_str == 0 && !value)
                {
                    if (type != DATA_NONE)
                    {
                        break;
                    }

                    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Value without key in the multipart formdata");
                }

                std::string line;

                // which buff contains \r or \n symbols ?
                const bool  first   = request_data.body_index < 2,
                            second  = request_data.body_index < 1;

                if (size_extra < first + second)
                {
                    THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "Size of the extra buffer eq -1. Maybe the recv data was not provided?");
                }

                line.append(buff_extra.data(), size_extra - (first + second));

                line.append(request_data.body_ptr + sizeof (char) * checkpoint, request_data.body_index - checkpoint + first + second - 2);

                const auto parsed_header = utils::parse_header(line);
                const auto header_value = utils::parse_header_value(parsed_header.second);

                if (parsed_header.first == HTTP_HEADER.CONTENT_DISPOSITION)
                {
                    if (header_value.empty())
                    {
                        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_IMPORTANT_HEADER_MISSING, "header value is empty: {}", HTTP_HEADER.CONTENT_DISPOSITION);
                    }

                    if (header_value[0].params.contains("name"))
                    {
                        type = DATA_PLAIN;
                        name = header_value[0].params.at("name");
                    }


                    if (header_value[0].params.contains("filename"))
                    {
                        // file
                        type = DATA_FILE;

                        file_data.file_name = header_value[0].params.at("filename");
                        file_data.param_name = std::move(name);
                    }

                    else {
                        // default param
                        param_data.first = std::move(name);
                    }
                }

                else if (parsed_header.first == HTTP_HEADER.CONTENT_TYPE)
                {
                    if (header_value.empty())
                    {
                        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_IMPORTANT_HEADER_MISSING, "{} can not be empty", HTTP_HEADER.CONTENT_TYPE);
                    }

                    if (DATA_FILE == type)
                    {
                        file_data.mime_type     = header_value[0].value;
                    }
                }

                checkpoint  = request_data.body_index;
                size_extra  = 0;
            }

            if (request_data.body_ptr[request_data.body_index] == body_boundary[boundary_index])
            {
                boundary_index ++;

                if (body_boundary.size() == boundary_index)
                {
                    boundary_index  = 2;
                    is_boundary     = true;

                    type = DATA_NONE;
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
                    const size_t can_back = std::min (request_data.body_index, boundary_index);
                    const size_t to_buff = boundary_index - can_back;
                    memcpy(buff_extra.data() + size_extra, body_boundary.data() + 2, to_buff);
                    size_extra += to_buff;
                    request_data.body_index -= can_back;
                    new_line = true;
                }

                boundary_index = 0;

                goto repeat;

            }
        }
    }
    catch (std::exception const &e) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PARSER_BUG, "Failed to parse the body of the request");
    }

    if (request_data.body_left == request_data.body_index) {
        type = DATA_NONE;
    }
}

void manapi::net::formdata_recv::urlencoded_read_param(const std::function<void(const char *, const size_t &)> &send_line) {
    size_t size_extra = 0;
    bool used_extra = false;

    std::string buffer;
    bool value = type != DATA_NONE;

    for (; request_data.body_index < request_data.body_left; request_data.body_index++) {
        // if (buffer.size() > config->get_partial_data_min_size()) {
        //     THROW_MANAPIHTTP_EXCEPTION2();
        // }

        if (request_data.body_index >= request_data.body_part) {
            request_data.body_left -= request_data.body_index;
            request_data.body_index =0;
            // get the next data
            ssize_t rhs = http_task->read (request_data.buffer.data(), request_data.buffer.size());
            if (rhs == -1) {
                THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "socket read error: read_next() = {}", rhs);
            }
            if (rhs > request_data.body_left) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_BODY_TOO_LONG, "http body too long. take it easy");
            }
            request_data.body_part = rhs;

            if (request_data.body_part == 0)
            {
                break;
            }

            request_data.body_part = std::min (request_data.body_part, request_data.body_left);
        }

        if (used_extra) {
            buff_extra[size_extra] = request_data.body_ptr[request_data.body_index];
            size_extra++;

            if (size_extra == 2) {
                const char c = (char) (manapi::net::utils::hex2dec(buff_extra[0]) << 4 | manapi::net::utils::hex2dec(buff_extra[1]));

                if (manapi::net::utils::valid_special_symbol(c))
                {
                    buffer += c;
                }
                else {
                    buffer.push_back('%');

                    buffer += buff_extra[0];
                    buffer += buff_extra[1];
                }

                size_extra  = 0;
                used_extra  = false;
            }

            continue;
        }

        if (request_data.body_ptr[request_data.body_index] == '%') {
            used_extra = true;

            continue;
        }

        if (request_data.body_ptr[request_data.body_index] == '=') {
            if (value) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "formdata: The Key already was defined");
            }
            param_data.first = std::move(buffer);
            buffer.clear();
            type = DATA_PLAIN;

            ++request_data.body_index;
            break;
        }

        if (request_data.body_ptr[request_data.body_index] == '&') {
            if (!value) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "formdata: Invalid sign '&' in the key space");
            }
            send_line (buffer.data(), buffer.size());
            buffer.clear();
            value = false;

            continue;
        }

        if (request_data.body_ptr[request_data.body_index] == '+') {
            buffer += '+';

            continue;
        }

        buffer += request_data.body_ptr[request_data.body_index];
    }

    if (request_data.body_index == request_data.body_left) {
        if (!value) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "formdata: Unexpected end of the urlencoded data"); }
        send_line (buffer.data(), buffer.size());
        type = DATA_NONE;
    }
}

manapi::net::file_data_t manapi::net::formdata_recv::about_file() const {
    if (DATA_FILE != type)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_NOT_CONTAINS_FILE, "{}", "No found any file in the body of the request");
    }

    return file_data;
}

std::string manapi::net::formdata_recv::get_file_to_str() {
    std::string content;

    get_file ([&] (const char *ptr, const size_t &size) {
        content.append(ptr, size);
    });

    return content;
}

void manapi::net::formdata_recv::get_file(const std::function<void(const char *, const size_t &)> &handler) {
    if (!next_file())
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_NOT_CONTAINS_FILE, "{}", "no file in the body of the request");
    }
    current_read_param (handler);
}

void manapi::net::formdata_recv::save_file (const std::string &filepath) {
    std::ofstream out (filepath);

    if (!out.is_open())
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Cannot open a file to write: {}", filepath);
    }

    get_file([&] (const char *ptr, const size_t & size) {
        out.write(ptr, (ssize_t) size);
    });

    out.close();
}

const std::string & manapi::net::formdata_recv::about_param() const {
    if (DATA_PLAIN != type)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_NOT_CONTAINS_FILE, "{}", "No found any param in the body of the request");
    }

    return param_data.first;
}

std::pair<std::string, std::string> manapi::net::formdata_recv::get_param() {
    if (!next_param()) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "No found any param in the body of the request");
    }
    auto n = std::move(param_data);
    current_read_param ([this, &n] (const char *buffer, const size_t &size) -> void {
        n.second.append(buffer, size);
    });
    return std::move(n);
}
