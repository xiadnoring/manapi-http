#include <memory.h>

#include "components/FormData.hpp"

#include "http/Base.hpp"
#include "ManapiHttpMime.hpp"

const std::string SPECIAL_SYMBOLS_BOUNDARY = "\r\n--";

manapi::net::formdata_recv::formdata_recv(request_data_t &request_data, std::shared_ptr<http::config> config, http::base *task) : http_task(task), request_data(&request_data), config(std::move(config)) {

}

manapi::net::formdata_recv::~formdata_recv() = default;

manapi::net::formdata_recv::formdata_recv(formdata_recv &&n) noexcept : request_data(n.request_data), config(std::move(n.config)), http_task(n.http_task) {
    this->_move(std::forward<decltype(n)>(n));
}

manapi::net::formdata_recv & manapi::net::formdata_recv::operator=(formdata_recv &&n) noexcept {
    this->config = std::move(n.config);
    this->http_task = n.http_task;
    this->request_data = n.request_data;
    this->_move(std::forward<decltype(n)>(n));
    n.http_task = nullptr;
    n.request_data = nullptr;
    return *this;
}

manapi::future<> manapi::net::formdata_recv::_init() {
    if (this->current_read_param != nullptr) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "FormData Parser was already initializated");
    }

    if (!this->request_data->has_body)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MISSING, "{}", "this method cannot have a body");
    }

    const auto header = utils::parse_header_value(this->request_data->headers.at(HTTP_HEADER.CONTENT_TYPE));

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

        this->body_boundary = SPECIAL_SYMBOLS_BOUNDARY + header[0].params.at("boundary");

        // the pointer to value of the param in a map params
        this->buff_extra.resize(std::max(4096UL, this->request_data->buffer.size()));
        this->content_type_form = CONTENT_TYPE_MULTIPART_FORM_DATA;
        // get the first metadata (name, type and etc)
        this->current_read_param = [this] (auto &param1) -> future<void> { co_await this->multipart_read_param (param1); co_return; };
        co_await this->current_read_param(nullptr);
    }
    else if (content_type == HTTP_MIME.APPLICATION_X_WWW_FORM_URLENCODED)
    {
        this->request_data->body_part = std::min (this->request_data->body_part, this->request_data->body_left);
        this->content_type_form = CONTENT_TYPE_APPLICATION_X_WWW_FORM_URLENCODED;
        this->buff_extra.resize(2);

        this->current_read_param = [this] (auto &param1) -> future<void> { co_await this->urlencoded_read_param (param1); co_return; };
        co_await this->current_read_param(nullptr);
    }
    else
    {
        this->content_type_form = CONTENT_TYPE_NONE;
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

manapi::future<void> manapi::net::formdata_recv::multipart_read_param (const std::function<void(const char *, const size_t &)> &send_line) {
    try {
        size_t  size_extra      = 0;

        this->request_data->body_part = std::min(this->request_data->body_part, this->request_data->body_left);

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
        size_t checkpoint = this->request_data->body_index;

        // boundary size which equal
        size_t boundary_index = first_line ? 2 : 0;

        // if it is a first line in the body
        if (first_line)
        {
            first_line = false;
        }

        for (;this->request_data->body_index < this->request_data->body_left; this->request_data->body_index++)
        {
            if (this->request_data->body_index >= this->request_data->body_part)
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
                        buff_to_extra_buff(*request_data, checkpoint, this->request_data->body_index, buff_extra, size_extra);
                    }

                    else
                    {

                        send_line(this->request_data->body_ptr + sizeof(char) * checkpoint,
                                  this->request_data->body_index - checkpoint);
                    }
                }

                else
                {
                    // dont +1 bcz body_index == socket block size
                    buff_to_extra_buff (*request_data, checkpoint, this->request_data->body_index, buff_extra, size_extra);
                }

                this->request_data->body_left -= this->request_data->body_index;
                this->request_data->body_index =0;
                // get the next data
                ssize_t rhs = co_await http_task->read (this->request_data->buffer.data(), this->request_data->buffer.size());
                if (rhs == -1) {
                    THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "socket read error: read_next(...) -> {}", rhs);
                }
                if (rhs > this->request_data->body_left) {
                    THROW_MANAPIHTTP_EXCEPTION (ERR_HTTP_BODY_TOO_LONG, "received data too long. {} > BODY_LEFT", rhs);
                }
                this->request_data->body_part = std::min(static_cast <size_t>(rhs), this->request_data->body_left);

                // read next block
                if (this->request_data->body_part == 0)
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
                        // if (size_extra > 0)
                        // {
                        //     buff_to_extra_buff (*request_data, checkpoint, this->request_data->body_index, buff_extra, size_extra);
                        //     const size_t result_size = size_extra - body_boundary.size() - 2;
                        //
                        //     send_line (buff_extra.data(), result_size);
                        // }
                        // else
                        {
                            size_t size_str = this->request_data->body_index - checkpoint - body_boundary.size() - 2;

                            send_line (this->request_data->body_ptr + sizeof (char) * checkpoint, size_str);
                        }

                        value = false;
                    }

                    checkpoint  = this->request_data->body_index;
                    size_extra  = 0;
                    if (this->request_data->body_size - 2 == this->request_data->body_index) { this->request_data->body_index+=2; }
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


                size_t size_str = this->request_data->body_index + size_extra;
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
                const bool  first   = this->request_data->body_index < 2,
                            second  = this->request_data->body_index < 1;

                if (size_extra < first + second)
                {
                    THROW_MANAPIHTTP_EXCEPTION(ERR_FATAL, "{}", "Size of the extra buffer eq -1. Maybe the recv data was not provided?");
                }

                line.append(buff_extra.data(), size_extra - (first + second));

                line.append(this->request_data->body_ptr + sizeof (char) * checkpoint, this->request_data->body_index - checkpoint + first + second - 2);

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
                        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_IMPORTANT_HEADER_MISSING, "{} can't be empty", HTTP_HEADER.CONTENT_TYPE);
                    }

                    if (DATA_FILE == type)
                    {
                        file_data.mime_type     = header_value[0].value;
                    }
                }

                checkpoint  = this->request_data->body_index;
                size_extra  = 0;
            }

            if (this->request_data->body_ptr[this->request_data->body_index] == body_boundary[boundary_index])
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
                    const size_t can_back = std::min (this->request_data->body_index, boundary_index);
                    const size_t to_buff = boundary_index - can_back;
                    memcpy(buff_extra.data() + size_extra, body_boundary.data() + 2, to_buff);
                    size_extra += to_buff;
                    this->request_data->body_index -= can_back;
                    new_line = true;
                }

                boundary_index = 0;

                goto repeat;

            }
        }
    }
    catch (std::exception const &e) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PARSER_BUG, "Failed to parse the body of the request by reason: {}", e.what());
    }

    if (this->request_data->body_left == this->request_data->body_index) {
        type = DATA_NONE;
    }
}

manapi::future<void> manapi::net::formdata_recv::urlencoded_read_param(const std::function<void(const char *, const size_t &)> &send_line) {
    size_t size_extra = 0;
    bool used_extra = false;

    std::string buffer;
    bool value = type != DATA_NONE;

    for (; this->request_data->body_index < this->request_data->body_left; this->request_data->body_index++) {
        // if (buffer.size() > config->get_partial_data_min_size()) {
        //     THROW_MANAPIHTTP_EXCEPTION2();
        // }

        if (this->request_data->body_index >= this->request_data->body_part) {
            this->request_data->body_left -= this->request_data->body_index;
            this->request_data->body_index =0;
            // get the next data
            ssize_t rhs = co_await http_task->read (this->request_data->buffer.data(), this->request_data->buffer.size());
            if (rhs == -1) {
                THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "socket read error: read_next() = {}", rhs);
            }
            if (rhs > this->request_data->body_left) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_BODY_TOO_LONG, "http body too long. take it easy");
            }
            this->request_data->body_part = rhs;

            if (this->request_data->body_part == 0)
            {
                break;
            }

            this->request_data->body_part = std::min (this->request_data->body_part, this->request_data->body_left);
        }

        if (!utils::uri_allowed_symbol(this->request_data->body_ptr[this->request_data->body_index])) {
            THROW_MANAPIHTTP_EXCEPTION (ERR_HTTP_PROTOCOL_ERROR, "Symbol '{}' is not allowed in URLEncoded FormData",
                static_cast<int>(this->request_data->body_ptr[this->request_data->body_index]));
        }

        if (used_extra) {
            buff_extra[size_extra] = this->request_data->body_ptr[this->request_data->body_index];
            size_extra++;

            if (size_extra == 2) {
                if (((buff_extra[0] >= '0' && buff_extra[0] <= '9') || (buff_extra[0] >= 'a' && buff_extra[0] <= 'z') || (buff_extra[0] >= 'A' && buff_extra[0] <= 'Z')) &&
                        ((buff_extra[1] >= '0' && buff_extra[1] <= '9') || (buff_extra[1] >= 'a' && buff_extra[1] <= 'z') || (buff_extra[1] >= 'A' && buff_extra[1] <= 'Z')))
                {
                    const char c = (char) (manapi::net::utils::hex2dec(buff_extra[0]) << 4 | manapi::net::utils::hex2dec(buff_extra[1]));
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

        if (this->request_data->body_ptr[this->request_data->body_index] == '%') {
            used_extra = true;

            continue;
        }

        if (this->request_data->body_ptr[this->request_data->body_index] == '=') {
            if (value) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "formdata: The Key already was defined");
            }
            param_data.first = std::move(buffer);
            buffer.clear();
            type = DATA_PLAIN;

            ++this->request_data->body_index;
            break;
        }

        if (this->request_data->body_ptr[this->request_data->body_index] == '&') {
            if (!value) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "formdata: Invalid sign '&' in the key space");
            }
            send_line (buffer.data(), buffer.size());
            buffer.clear();
            value = false;

            continue;
        }

        if (this->request_data->body_ptr[this->request_data->body_index] == '+') {
            buffer += '+';

            continue;
        }

        buffer += this->request_data->body_ptr[this->request_data->body_index];
    }

    if (this->request_data->body_index == this->request_data->body_left) {
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

manapi::future<std::string> manapi::net::formdata_recv::get_file_to_str() {
    std::string content;

    co_await get_file ([&] (const char *ptr, const size_t &size) {
        content.append(ptr, size);
    });

    co_return std::move(content);
}

manapi::future<void> manapi::net::formdata_recv::get_file(const std::function<void(const char *, const size_t &)> &handler) {
    if (!next_file())
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_NOT_CONTAINS_FILE, "{}", "no file in the body of the request");
    }
    co_await current_read_param (handler);
}

manapi::future<void> manapi::net::formdata_recv::save_file (const std::string &filepath) {
    std::ofstream out (filepath);

    if (!out.is_open())
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Cannot open a file to write: {}", filepath);
    }

    co_await get_file([&] (const char *ptr, const size_t & size) {
        out.write(ptr, static_cast<std::streamsize> (size));
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

manapi::future<std::pair<std::string, std::string>> manapi::net::formdata_recv::get_param() {
    if (!next_param()) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "No found any param in the body of the request");
    }
    auto n = std::move(param_data);
    co_await current_read_param ([this, &n] (const char *buffer, const size_t &size) -> void {
        n.second.append(buffer, size);
    });
    co_return std::move(n);
}

void manapi::net::formdata_recv::_move(formdata_recv &&n) noexcept {
    this->body_boundary = std::move(n.body_boundary);
    this->buff_extra = std::move(n.buff_extra);
    this->file_data = std::move(n.file_data);
    this->first_line = n.first_line;
    this->param_data = std::move(n.param_data);
    this->type = n.type;
    this->content_type_form = n.content_type_form;

    switch (this->content_type_form) {
        case CONTENT_TYPE_MULTIPART_FORM_DATA:
            this->current_read_param = [this] (auto &param1) -> future<void> { co_await this->multipart_read_param (param1); co_return; };
        break;
        case CONTENT_TYPE_APPLICATION_X_WWW_FORM_URLENCODED:
            this->current_read_param = [this] (auto &param1) -> future<void> { co_await this->urlencoded_read_param (param1); co_return; };
        break;
        default:
            break;
    }

    n.current_read_param = nullptr;
    n.type = DATA_NONE;
    n.first_line = true;
}
