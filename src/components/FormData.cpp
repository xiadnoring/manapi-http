#include <memory.h>

#include "components/FormData.hpp"

#include "ManapiFilesystem.hpp"
#include "http/Utils.hpp"
#include "ManapiHttpMime.hpp"
#include "ManapiUnicode.hpp"
#include "crypto/ManapiURL.hpp"
#include "ManapiHttpTypes.hpp"
#include "ManapiString.hpp"
#include "http/base_http.hpp"

const std::string SPECIAL_SYMBOLS_BOUNDARY = "\r\n--";
constexpr ssize_t line_max_size = 500;

manapi::net::formdata_recv::formdata_recv(std::shared_ptr<async::context> ctx, size_t buffer_size,
            ssize_t &body_buffer_size, char *buffer, ssize_t &body_max_size_left, ssize_t &body_index, std::function<future<ssize_t>(void *, ssize_t)> body_read) : ctx(std::move(ctx)) {
    this->body_index = &body_index;
    this->body_buffer = buffer;
    this->body_buffer_size = &body_buffer_size;
    this->body_max_size_left = &body_max_size_left;
    this->body_read = std::move(body_read);
    this->buffer_size = buffer_size;
}

manapi::net::formdata_recv::~formdata_recv() = default;

manapi::net::formdata_recv::formdata_recv(formdata_recv &&n) noexcept {
    this->_move(std::forward<decltype(n)>(n));
}

manapi::net::formdata_recv & manapi::net::formdata_recv::operator=(formdata_recv &&n) noexcept {
    this->_move(std::forward<decltype(n)>(n));
    return *this;
}

manapi::future<> manapi::net::formdata_recv::_init(bool has_body, const std::string &content_type) {
    if (this->current_read_param) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_FATAL, "FormData Parser was already initializated");
    }

    if (!has_body)
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_MISSING, "{}", "this method cannot have a body");
    }

    const auto header = http::parse_header_value(content_type);

    if (header.empty())
    {
        THROW_MANAPIHTTP_EXCEPTION (ERR_HTTP_CONTENT_TYPE_MISSING, "header value is empty: {}", http::HEADER.CONTENT_TYPE);
    }

    auto &content_type_value = header[0].value;
    *this->body_buffer_size = std::min(*this->body_buffer_size, *this->body_max_size_left);

    if (content_type_value == mime::types.MULTIPART_FORM_DATA)
    {
        if (!header[0].params.contains("boundary"))
        {
            THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_BOUNDARY_MISSING, "{}", "boundary not found");
        }

        this->body_boundary = SPECIAL_SYMBOLS_BOUNDARY + header[0].params.at("boundary");

        this->buff_extra = {};
        this->content_type_form = CONTENT_TYPE_MULTIPART_FORM_DATA;
        // get the first metadata (name, type and etc)
        this->current_read_param = [this] (auto param1) -> future<void> { co_await this->multipart_read_param (std::move(param1)); co_return; };
        co_await this->current_read_param(nullptr);
    }
    else if (content_type_value == mime::types.APPLICATION_X_WWW_FORM_URLENCODED)
    {
        this->content_type_form = CONTENT_TYPE_APPLICATION_X_WWW_FORM_URLENCODED;
        this->buff_extra.resize(2);

        this->current_read_param = [this] (auto param1) -> future<void> { co_await this->urlencoded_read_param (std::move(param1)); co_return; };
        co_await this->current_read_param(nullptr);
    }
    else
    {
        this->content_type_form = CONTENT_TYPE_NONE;
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_INVALID_CONTENT_TYPE, "Invalid POST DATA MIME-type: {}", content_type_value);
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
    return this->type == DATA_FILE;
}

bool manapi::net::formdata_recv::next_param() const {
    return this->type == DATA_PLAIN;
}

manapi::future<void> manapi::net::formdata_recv::multipart_read_param (std::function<manapi::future<>(const char *, ssize_t)> send_line) {
    try {
        bool headers = false;
        bool value = false;
        int carret = 0;

        std::string line;

        if (this->type != DATA_NONE) {
            // we parse value for now
            value = true;
            headers = false;
        }

        // the start of the string
        ssize_t checkpoint = *this->body_index;

        // boundary size which equal
        ssize_t boundary_index = std::exchange(this->first_line, false) * 2;
        ssize_t current_boundary_index = 0;

        while(true) {
            if (*this->body_index >= *this->body_buffer_size) {
                if (*this->body_index > checkpoint + current_boundary_index) {
                    if (value) {
                        co_await send_line (this->body_buffer + checkpoint, *this->body_index - checkpoint - current_boundary_index);
                    }
                    else if (headers) {
                        auto n = *this->body_index - checkpoint - current_boundary_index;
                        if (n + line.size() > line_max_size) {
                            THROW_MANAPIHTTP_EXCEPTION2 (ERR_HTTP_PROTOCOL_ERROR, "buffer overflow");
                        }
                        line.append (this->body_buffer + checkpoint, n);
                    }
                }

                if (!(*this->body_max_size_left -= *this->body_index)) {
                    /**
                     * TODO: check boundary end (--AAXXX--\r\n)
                     */
                    this->type = DATA_NONE;
                    break;
                }

                auto rhs = co_await this->body_read (this->body_buffer, static_cast<ssize_t>(this->buffer_size));
                if (rhs <= 0) {
                    THROW_MANAPIHTTP_EXCEPTION2(ERR_FILE_IO, "FormData: Connection was closed");
                }
                *this->body_index = 0;
                *this->body_buffer_size = rhs;
                current_boundary_index = 0;
                checkpoint = 0;
            }

            auto &c = *(this->body_buffer + *this->body_index);
            if (c == this->body_boundary[boundary_index]) {
                ++boundary_index;
                ++current_boundary_index;
                if (boundary_index == this->body_boundary.size()) {
                    if (value) {
                        if (*this->body_index + 1 > current_boundary_index + checkpoint) {
                            co_await send_line (this->body_buffer + checkpoint, *this->body_index + 1 - current_boundary_index - checkpoint);
                        }

                        value = false;
                    }

                    checkpoint = *this->body_index + 1;
                    boundary_index = 0;
                    current_boundary_index = 0;
                }
            }
            else {
                ssize_t boundary_size = 0;
                if (boundary_index > 1) {
                    boundary_size = boundary_index;
                    carret = 1;
                }
                else if (boundary_index > 0) {
                    boundary_size = boundary_index;
                }

                if (boundary_index) {
                    if (boundary_index < *this->body_index) {
                        if (value) {
                            goto skip;
                            co_await send_line (this->body_buffer + checkpoint, *this->body_index - checkpoint - boundary_size);
                        }
                        else {
                            auto n = *this->body_index - checkpoint - boundary_size;
                            if (n + line.size() > line_max_size) {
                                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "buffer overflow");
                            }
                            line.append (this->body_buffer + checkpoint, n);
                        }
                    }

                    if (value) {
                        co_await send_line (this->body_boundary.data(), boundary_size);
                    }
                    else {
                        auto n = boundary_size - 2;
                        if (n + line.size() > line_max_size) {
                            THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "buffer overflow");
                        }
                        line.append(this->body_boundary.data(), n);
                    }

                    checkpoint = *this->body_index;
                    skip:
                    current_boundary_index = 0;
                    boundary_index = 0;
                }


                if (value) {
                    carret = 0;
                }
                else {
                    if (carret) {
                        carret = 0;

                        if (headers) {
                            if (line.empty()) {
                                value = true;
                                headers = false;
                                break;
                            }

                            auto header_data = http::parse_header(line);
                            auto header_value = http::parse_header_value(header_data.second);

                            if (header_value.empty()) {
                                THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "{} header is empty", header_data.first);
                            }

                            if (header_data.first == http::HEADER.CONTENT_DISPOSITION) {
                                if (header_value[0].value != "form-data" || !header_value[0].params.contains("name")) {
                                    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Content-Disposition is invalid");
                                }

                                std::string name = std::move(header_value[0].params["name"]);
                                if (header_value[0].params.contains("filename")) {
                                    this->type = DATA_FILE;
                                    this->file_data.param_name = std::move(name);
                                    this->file_data.file_name = std::move(header_value[0].params["filename"]);
                                }
                                else {
                                    this->type = DATA_PLAIN;
                                    this->param_data.first = std::move(name);
                                }
                            }
                            else if (header_data.first == http::HEADER.CONTENT_TYPE) {
                                switch (this->type) {
                                    case DATA_FILE:
                                        this->file_data.mime_type = std::move(header_value[0].value);
                                    break;
                                    default:
                                        THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Content-Type not allowed for text/plain param");
                                }
                            }
                            else {

                            }

                            line.clear();
                        }
                        else {
                            headers = true;
                        }

                       continue;
                    }
                }
            }

            (*this->body_index)++;
        }
    }
    catch (std::exception const &e) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PARSER_BUG, "Failed to parse the body of the request by reason: {}", e.what());
    }

    if (*this->body_max_size_left == *this->body_index) {
        this->type = DATA_NONE;
    }
}

manapi::future<void> manapi::net::formdata_recv::urlencoded_read_param(std::function<manapi::future<>(const char *, ssize_t)> send_line) {
    size_t size_extra = 0;
    bool used_extra = false;

    std::string buffer;
    bool value = type != DATA_NONE;

    for (; *this->body_index < *this->body_max_size_left; (*this->body_index)++) {
        // if (buffer.size() > config->get_partial_data_min_size()) {
        //     THROW_MANAPIHTTP_EXCEPTION2();
        // }

        if (*this->body_index >= *this->body_buffer_size) {
            *this->body_max_size_left -= *this->body_index;
            *this->body_index = 0;
            // get the next data
            ssize_t rhs = co_await this->body_read (this->body_buffer, static_cast<ssize_t>(this->buffer_size));
            if ((rhs <= 0)) {
                THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "socket read error: read_next() = {}", rhs);
            }
            if (rhs > *this->body_max_size_left) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_BODY_TOO_LONG, "http body too long. take it easy");
            }
            *this->body_buffer_size = rhs;
            *this->body_buffer_size = std::min (*this->body_buffer_size, *this->body_max_size_left);
        }

        auto &c = *(this->body_buffer + *this->body_index);

        if (!manapi::crypto::url_allowed_symbol(c)) {
            THROW_MANAPIHTTP_EXCEPTION (ERR_HTTP_PROTOCOL_ERROR, "Symbol '{}' is not allowed in URLEncoded FormData",
                static_cast<int>(c));
        }

        if (used_extra) {
            this->buff_extra[size_extra] = c;
            size_extra++;

            if (size_extra == 2) {
                if (((this->buff_extra[0] >= '0' && this->buff_extra[0] <= '9') || (this->buff_extra[0] >= 'a' && this->buff_extra[0] <= 'z') || (this->buff_extra[0] >= 'A' && this->buff_extra[0] <= 'Z')) &&
                        ((this->buff_extra[1] >= '0' && this->buff_extra[1] <= '9') || (this->buff_extra[1] >= 'a' && this->buff_extra[1] <= 'z') || (this->buff_extra[1] >= 'A' && this->buff_extra[1] <= 'Z')))
                {
                    const char c2 = (char) (manapi::unicode::hex2dec(this->buff_extra[0]) << 4 | manapi::unicode::hex2dec(this->buff_extra[1]));
                    buffer += c2;
                }
                else {
                    buffer.push_back('%');

                    buffer += this->buff_extra[0];
                    buffer += this->buff_extra[1];
                }

                size_extra  = 0;
                used_extra  = false;
            }

            continue;
        }

        if (c == '%') {
            used_extra = true;

            continue;
        }

        if (c == '=') {
            if (value) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "formdata: The Key already was defined");
            }
            param_data.first = std::move(buffer);
            buffer.clear();
            type = DATA_PLAIN;

            ++(*this->body_index);
            break;
        }

        if (c == '&') {
            if (!value) {
                THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "formdata: Invalid sign '&' in the key space");
            }
            co_await send_line (buffer.data(), static_cast<ssize_t>(buffer.size()));
            buffer.clear();
            value = false;

            continue;
        }

        if (c == '+') {
            buffer += ' ';

            continue;
        }

        buffer += c;
    }

    if (*this->body_index == *this->body_max_size_left) {
        if (!value) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "formdata: Unexpected end of the urlencoded data"); }
        co_await send_line (buffer.data(), static_cast<ssize_t>(buffer.size()));
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

    co_await get_file ([&] (const char *ptr, ssize_t size) {
        content.append(ptr, size);
    });

    co_return std::move(content);
}

manapi::future<void> manapi::net::formdata_recv::get_file(std::function<void(const char *, ssize_t)> handler) {
    if (!next_file()) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_NOT_CONTAINS_FILE, "{}", "no file in the body of the request");
    }
    co_await this->current_read_param ([handler = std::move(handler)] (const char *buff, ssize_t size)
        -> manapi::future<> { handler (buff, size); co_return; });
}

manapi::future<> manapi::net::formdata_recv::get_async_file(std::function<manapi::future<>(const char *, ssize_t)> handler) {
    if (!next_file()) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_BODY_NOT_CONTAINS_FILE, "{}", "no file in the body of the request");
    }

    co_await this->current_read_param (std::move(handler));
}

manapi::future<void> manapi::net::formdata_recv::save_file (std::string filepath) {
    manapi::filesystem::async::fstream out (this->ctx, filepath);
    co_await out.open(out.FILE_WRITE|out.FILE_CREATE|out.FILE_TRUNC);
    if (!out.is_open())
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Cannot open a file to write: {}", filepath);
    }

    std::exception_ptr err{nullptr};

    try {
        co_await get_async_file ([&] (const char *ptr, ssize_t size)
            -> manapi::future<> { return out.fwrite(ptr, size); });
    }
    catch (...) {
        err = std::current_exception();
    }

    co_await out.close();

    if (err) {
        std::rethrow_exception(std::move(err));
    }
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
    co_await current_read_param ([this, &n] (const char *buffer, ssize_t size)
        -> manapi::future<> { n.second.append(buffer, size); co_return; });
    co_return std::move(n);
}

std::string manapi::net::formdata_recv::json2form(const json &obj) {
    if (!obj.is_object())
    {
        THROW_MANAPIHTTP_EXCEPTION(ERR_UNSUPPORTED, "{}", "the arg must be json object in json2form(...)");
    }

    std::string data;
    auto it = obj.begin<json::OBJECT>();
    goto loop;

    for (; it != obj.end<json::OBJECT>(); ++it)
    {
        data += '&';
        loop:
        if (it->second.is_string())
        {
            data += crypto::encode_url(it->first) + "=" + crypto::encode_url(it->second.as_string());
        }
        else
        {
            data += crypto::encode_url(it->first) + "=" + crypto::encode_url(it->second.dump());
        }

    }
    return std::move(data);
}

void manapi::net::formdata_recv::_move(formdata_recv &&n) noexcept {
    this->body_boundary = std::move(n.body_boundary);
    this->buff_extra = std::move(n.buff_extra);
    this->file_data = std::move(n.file_data);
    this->first_line = std::exchange(n.first_line, true);
    this->param_data = std::move(n.param_data);
    this->type = std::exchange(n.type, DATA_NONE);
    this->ctx = std::move(n.ctx);
    this->buffer_size = std::exchange(n.buffer_size, 0);
    this->content_type_form = n.content_type_form;
    this->body_index = std::exchange(n.body_index, nullptr);
    this->body_max_size_left = std::exchange(n.body_max_size_left, nullptr);
    this->body_buffer = std::exchange(n.body_buffer, nullptr);
    this->body_buffer_size = std::exchange(n.body_buffer_size, nullptr);
    this->body_read = std::move(n.body_read);

    switch (this->content_type_form) {
        case CONTENT_TYPE_MULTIPART_FORM_DATA:
            this->current_read_param = [this] (auto param1) -> future<void> { co_await this->multipart_read_param (std::move(param1)); co_return; };
        break;
        case CONTENT_TYPE_APPLICATION_X_WWW_FORM_URLENCODED:
            this->current_read_param = [this] (auto param1) -> future<void> { co_await this->urlencoded_read_param (std::move(param1)); co_return; };
        break;
        default:
            break;
    }

    n.current_read_param = nullptr;
}

constexpr int boundary_payload_size = 32;
constexpr char boundary_end_symbols[] = "--";
constexpr char nline[] = "\r\n";

manapi::net::formdata_send::formdata_send(std::shared_ptr<async::context> ctx) : ctx(std::move(ctx)) {}

manapi::net::formdata_send::~formdata_send() {
}

manapi::net::formdata_send::formdata_send(formdata_send &&n) noexcept {
    this->operator=(std::forward<decltype(n)>(n));
}

manapi::net::formdata_send & manapi::net::formdata_send::operator=(formdata_send &&n) noexcept {
    this->ctx = std::move(n.ctx);
    this->data = std::move(n.data);
    return *this;
}

void manapi::net::formdata_send::append_file(const std::string &name, std::string filepath) {
    auto filename = manapi::filesystem::basename(filepath);
    auto filemime = manapi::mime::mime_by_file_path(filename);

    this->data.insert({name,  {DATA_FILE, std::move(filepath), data_file_storage{std::move(filename), std::move(filemime)}}});
}

void manapi::net::formdata_send::append_file(const std::string &name, std::string filepath, std::string filename, std::string filemime) {
    this->data.insert({name,  {DATA_FILE, std::move(filepath), data_file_storage{std::move(filename), std::move(filemime)}}});
}

void manapi::net::formdata_send::append_text(const std::string &name, std::string data) {
    this->data.insert({name, {DATA_PLAIN, std::move(data), {}}});
}

void manapi::net::formdata_send::erase(const std::string &name) {
    this->data.erase(name);
}

bool manapi::net::formdata_send::contains(const std::string &name) const {
    return this->data.contains(name);
}

ssize_t manapi::net::formdata_send::payload_size() const {
    ssize_t s = 0;
    for (const auto &param : this->data) {
        switch (param.second.type) {
            case DATA_FILE:
                s += manapi::filesystem::get_size(param.second.data);
            break;
            case DATA_PLAIN:
                s += static_cast<ssize_t>(param.second.data.size());
            break;
            default:
                break;
        }
    }
    return s;
}

ssize_t manapi::net::formdata_send::multipart_size(ssize_t boundary_size) const {
    auto s = static_cast<ssize_t>(boundary_size + (sizeof ("--\r\n") - 1));
    for (const auto &param : this->data) {
        s += static_cast<ssize_t>(boundary_size + (sizeof ("\r\n") - 1));
        if (param.second.type == DATA_PLAIN) {
            std::string header = http::stringify_header({http::HEADER.CONTENT_DISPOSITION,
                http::stringify_header_value({{"form-data", {{"name", json{param.first}.dump()}}}})});
            s += static_cast<ssize_t> (header.size());
            s += (sizeof ("\r\n") - 1);
        }
        else if (param.second.type == DATA_FILE) {
            std::string header = http::stringify_header({http::HEADER.CONTENT_DISPOSITION,
                http::stringify_header_value({{"form-data", {{"name", json{param.first}.dump()},
                    {"filename", json{param.second.file.value().filename}.dump()}}}})});
            s += static_cast<ssize_t> (header.size());
            s += (sizeof ("\r\n") - 1);

            header = http::stringify_header({http::HEADER.CONTENT_TYPE,
                http::stringify_header_value({{param.second.file.value().filemime}})});
            s += static_cast<ssize_t> (header.size());
            s += (sizeof ("\r\n") - 1);
        }

        s += (sizeof ("\r\n") - 1);
        /* ... */
        s += (sizeof ("\r\n") - 1);
    }
    return s;
}

std::string manapi::net::formdata_send::generate_boundary() const {
    return "--boundary" + manapi::string::random(boundary_payload_size, "qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM0123456789");
}

manapi::future<> manapi::net::formdata_send::data2multipart(std::string boundary, ssize_t buffer_size,  std::function<manapi::future<void>(const void *buffer, ssize_t size)> write) {
    for (auto &param : this->data) {
        co_await write (boundary.data(), static_cast<ssize_t>(boundary.size()));
        co_await write (nline, sizeof (nline) - 1);

        if (param.second.type == DATA_PLAIN) {
            std::string header = http::stringify_header({http::HEADER.CONTENT_DISPOSITION,
                http::stringify_header_value({{"form-data", {{"name", json{param.first}.dump()}}}})});

            co_await write (header.data(), static_cast<ssize_t>(header.size()));
            co_await write (nline, sizeof (nline) - 1);

            co_await write (nline, sizeof (nline) - 1);

            co_await write (param.second.data.data(), static_cast<ssize_t> (param.second.data.size()));
            param.second.data = {};

            co_await write (nline, sizeof (nline) - 1);
        }

        if (param.second.type == DATA_FILE) {
            std::string header = http::stringify_header({http::HEADER.CONTENT_DISPOSITION,
                http::stringify_header_value({{"form-data", {{"name", json{param.first}.dump()},
                    {"filename", json{std::move(param.second.file.value().filename)}.dump()}}}})});
            co_await write (header.data(), static_cast<ssize_t>(header.size()));
            co_await write (nline, sizeof (nline) - 1);

            header = http::stringify_header({http::HEADER.CONTENT_TYPE,
                http::stringify_header_value({{std::move(param.second.file.value().filemime)}})});
            co_await write (header.data(), static_cast<ssize_t>(header.size()));
            co_await write (nline, sizeof (nline) - 1);

            co_await write (nline, sizeof (nline) - 1);

            manapi::filesystem::async::fstream f (this->ctx, param.second.data);
            co_await f.open(f.FILE_READ);

            if (!f.is_open()) {
                THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Failed to read file ({}) to send it as form data parameter", param.second.data);
            }

            std::exception_ptr err{nullptr};

            std::string buffer;
            buffer.reserve(buffer_size);

            try {
                auto fsize = f.total_size();

                while (fsize) {
                    auto rhs = co_await f.read(buffer.data(), buffer_size);
                    if (rhs < 0) {
                        THROW_MANAPIHTTP_EXCEPTION(ERR_FILE_IO, "Failed to read file ({}) to send it as formdata parameter", param.second.data);
                    }
                    if (rhs == 0) {
                        continue;
                    }

                    fsize -= rhs;

                    co_await write (buffer.data(), rhs);
                }
            }
            catch (...) {
                err = std::current_exception();
            }

            co_await f.close();

            if (err) {
                std::rethrow_exception(std::move(err));
            }

            param.second.data = {};

            co_await write (nline, sizeof (nline) - 1);
        }
    }

    co_await write (boundary.data(), static_cast<ssize_t>(boundary.size()));
    co_await write (boundary_end_symbols, sizeof (boundary_end_symbols) - 1);
    co_await write (nline, sizeof (nline) - 1);
}






