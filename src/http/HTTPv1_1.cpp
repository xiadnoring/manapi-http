#include "http/HTTPv1_1.hpp"

#include "ManapiFilesystem.hpp"
#include "services/ManapiFetch.hpp"
#include "http/HeaderView.hpp"

manapi::net::http::http_v1_1::http_v1_1(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site) : base(std::move(worker), std::move(config), site) {}

manapi::net::http::http_v1_1::~http_v1_1() = default;

std::shared_ptr<manapi::net::http::http_v1_1> manapi::net::http::http_v1_1::create(
    std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config,
    manapi::net::site &site) {
    return std::make_shared<manapi::net::http::http_v1_1>(std::move(worker), std::move(config), site);
}

void manapi::net::http::http_v1_1::doit() {

}

manapi::future<bool> manapi::net::http::http_v1_1::parse_request(ssize_t j, ssize_t size) {
    this->request_data.body_index = 0;

    if (!this->buffer) {
        this->buffer = this->site.bufferpool().get();
    }

    this->buffer->resize(this->config->buffer_size().load());

    {
        if (size == 0) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "HTTP Status hasn't been parsed");
        }

        this->current = [this](auto && PH1) { _parse_headers(std::forward<decltype(PH1)>(PH1)); };
        goto skip;

        while (!this->parse_vars.finished) {
            size = co_await this->worker->read (*this->connection, this->buffer->data(), static_cast<ssize_t>(this->buffer->size()));
            if (size <= 0) { break; }
            j = 0;
            skip: for (; j < size && !this->parse_vars.finished; j++) {
                this->current (this->buffer->at(j));
            }
        }

        j++;
    }

    ssize_t content_length = 0;
    bool transfer_encoding_chunked = false;

    if (this->request_data.headers.contains(HEADER.TRANSFER_ENCODING)) {
        const auto header_value = parse_header_value(this->request_data.headers[HEADER.TRANSFER_ENCODING]);
        for (const auto &param : header_value) {
            if (param.value == "chunked") {
                transfer_encoding_chunked = true;
            }
            else {
                /* brotli, gzip and etc isn't supported yet */
                MANAPIHTTP_LOG("request was declined with {} status code because the request transfer-encoding header "
                               "contains the unsupported value: {}", static_cast<int>(PRECONDITION_FAILED_412), param.value);

                const auto handler = this->site.handler(this->request_data);
                co_await this->send_error_response(PRECONDITION_FAILED_412, this->request_data, handler.error.get());

                co_return false;
            }
        }
    }

    if (this->request_data.headers.contains(HEADER.CONTENT_LENGTH)) {
        content_length = std::stoll(this->request_data.headers[HEADER.CONTENT_LENGTH]);
    }
    else if (transfer_encoding_chunked) {
        /* content length isn't fixed */
        content_length = -1;
    }

    this->request_data.has_body = content_length == -1 || content_length > 0;
    this->request_data.buffer = std::move(this->buffer);
    this->read_async = [this, transfer_encoding_chunked] (void *buffer, ssize_t size)
        -> manapi::future<ssize_t> {
        auto data = this;
        if (co_await data->expect_header()) {}

        if (transfer_encoding_chunked) {
            data->read_async = [data, left = static_cast<ssize_t>(0), state = 0, prev = std::string()] (void *buffer, ssize_t size) mutable
                -> manapi::future<ssize_t> {
                ssize_t i = 0;
                ssize_t tmp = size;

                while (true) {
                    if (left == -1) {
                        /* end of chunk stream */
                        break;
                    }

                    if (state != 2) {
                        /* accept \r\n */
                        if (prev.empty()) {
                            prev.resize(20);
                            auto rhs = co_await data->worker->read (*data->connection, prev.data(), static_cast<ssize_t>(prev.size()));
                            if (rhs < 0) { co_return rhs; }
                            prev.resize(rhs);
                        }
                    }
                    if (state < 2) {
                        /* accept length number and \r\n */
                        for (; i < prev.size(); i++) {
                            auto c = tolower(prev[i]);

                            if (left > /* TODO: set it using the config */ 200000) {
                                THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "read(...): Transfer-Encoding chunk is too large to be "
                                                                                    "accepted. Max Size: {}", 200000);
                            }

                            if (0 == state) {
                                if (c >= '0' && c <= '9') {
                                    left *= 16; left += (c - '0'); continue;
                                }
                                if (c >= 'a' && c <= 'f') {
                                    left *= 16; left += c - 'a' + 10; continue;
                                }

                                if (c == '\r') {
                                    state = 1; continue;
                                }
                            }
                            if (1 == state && c == '\n') {
                                state = 2;
                                i++;

                                auto copy = std::min(static_cast<ssize_t>(prev.size()) - i, left);
                                memcpy(buffer, prev.data() + i, copy);

                                left -= copy;

                                if (!left) {
                                    /* already copied */
                                    if (copy) {
                                        state = 3;
                                        i += copy;
                                    }
                                    else {
                                        /* end of chunk stream */
                                        state = 3;
                                        left = -1;
                                    }
                                }
                                else {
                                    prev.resize(0);
                                    i = copy;
                                }

                                break;
                            }

                            THROW_MANAPIHTTP_EXCEPTION2 (ERR_HTTP_PROTOCOL_ERROR, "read(...): Transfer-Encoding rules must be respected, but the request "
                                                                                  "violated them");
                        }

                        if (i == prev.size() && state != 2) {
                            prev.resize(0);
                            i = 0;
                            continue;
                        }
                    }
                    if (2 == state) {
                        auto rhs = co_await data->worker->read (*data->connection, static_cast<char *>(buffer) + i, std::min(size - i, left));
                        i = 0;
                        if (rhs < 0) { co_return rhs; }
                        left -= rhs;
                        size -= rhs;
                        if (!left) {
                            state = 3;
                        }
                    }

                    if (state > 2) {
                        if (prev.empty()) {
                            continue;
                        }

                        for (; i < prev.size(); i++) {
                            if (state == 3 && prev[i] == '\r') {
                                state = 4;
                                continue;
                            }
                            if (state == 4 && prev[i] == '\n') {
                                state = 0;
                                i++;

                                break;
                            }
                            THROW_MANAPIHTTP_EXCEPTION2 (ERR_HTTP_PROTOCOL_ERROR, "read(...): Transfer-Encoding: rules must be respected, but the request "
                                                                                "violated them");
                        }

                        if (prev.size() == i) {
                            prev.resize(0);
                            i = 0;
                        }

                        continue;
                    }

                    break;
                }

                co_return tmp - size;
            };
        }
        else {
            data->read_async = [data] (void *buffer, ssize_t size)
                -> manapi::future<ssize_t> { co_return co_await data->worker->read (*data->connection, buffer, size); };
        }

        co_return co_await data->read_async(buffer, size);
    };

    if (this->request_data.has_body) {
        this->request_data.body_size = content_length;

        if (size <= j) {
            size = 0;
            j = 0;
        }

        this->request_data.headers_part = j;
        this->request_data.body_part = size;
        this->request_data.body_index = j;
        if (this->request_data.body_size >= 0) {
            this->request_data.body_left = this->request_data.body_size + j;
        }
        else {
            /* content length isn't fixed */
            this->request_data.body_left = -1;
        }
    }
    else {
        this->request_data.body_index = 0;
        this->request_data.body_size = 0;
        this->request_data.body_left = 0;
    }

    /* try upgrade */
    this->upgraded = co_await upgrade_connection();

    if (!co_await this->validate_http_version()) {
        co_return false;
    }

    co_return true;
}

manapi::future<void> manapi::net::http::http_v1_1::execute_handler() {
    const auto handler = this->site.handler(this->request_data);
    co_await handle_request(&handler, this->request_data);
    co_return;
}

bool manapi::net::http::http_v1_1::connection_was_upgraded() const {
    return this->upgraded != versions::HTTP_v1_1;
}

int manapi::net::http::http_v1_1::upgraded_version() const {
    return this->upgraded;
}

manapi::future<ssize_t> manapi::net::http::http_v1_1::read(void *buffer, ssize_t size) {
    return this->read_async(buffer, size);
}

manapi::future<bool> manapi::net::http::http_v1_1::validate_http_version() {
    if (this->connection_was_upgraded()) {
        co_return true;
    }

    co_return co_await base::validate_http_version();
}

void manapi::net::http::http_v1_1::_skip_white_space(char &c) {
    if (c == ' ') {
        this->current = this->next;
        return;
    }
    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid char");
}

void manapi::net::http::http_v1_1::_next_line(char &c) {
    if (this->parse_vars.next_line_state) {
        this->parse_vars.next_line_state = false;
        if (c == '\n') {
            this->current = next;
            return;
        }
    }
    else {
        if (c == '\r') {
            this->parse_vars.next_line_state = true;
            if (this->parse_vars.dbl) {
                this->parse_vars.finished = true;
            }
            return;
        }
    }

    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid symbol");
}

void manapi::net::http::http_v1_1::_parse_headers(char &c) {
    if (c == '\r') {
        this->parse_vars.is_key = true;
        if (!this->parse_vars.key.empty()) {
            this->parse_vars.key = "";
        }


        this->current = [this](auto && PH1) { _next_line(std::forward<decltype(PH1)>(PH1)); };
        this->next = [this](auto && PH1) { _parse_headers(std::forward<decltype(PH1)>(PH1)); };
        this->current(c);

        this->parse_vars.dbl = true;

        return;
    }

    if (this->parse_vars.dbl) {
        this->parse_vars.dbl = false;
    }

    if (c == ':' && this->parse_vars.is_key) {
        this->parse_vars.is_key = false;
        this->parse_vars.value = &this->request_data.headers[this->parse_vars.key];

        return;
    }



    if (this->parse_vars.is_key) {
        this->parse_vars.key += static_cast<char>(std::tolower(c));
    } else {
        if (c == ' ' && this->parse_vars.value->empty()) {
            return;
        }
        if (!this->parse_vars.value) {
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_HTTP_PROTOCOL_ERROR, "error in the headers section");
        }
        *this->parse_vars.value += c;
    }
}

int manapi::net::http::http_v1_1::flags() const {
    return this->flags_;
}

manapi::future<manapi::net::http::versions::http> manapi::net::http::http_v1_1::upgrade_connection() {
    http::versions::http toupgrade = versions::HTTP_v1_1;

    if (this->request_data.headers.contains(HEADER.CONNECTION)) {
        const auto connection_header = http::parse_header_value(this->request_data.headers[HEADER.CONNECTION]);
        for (const auto &param: connection_header) {
            if (param.value == "Upgrade") {
                if (this->request_data.headers[HEADER.UPGRADE] == "h2c") {
                    toupgrade = versions::HTTP_v2;
                }
                continue;
            }
            if (param.value == "HTTP2-Settings") {
                //http2settings = request_data.headers["http2-settings"];
                continue;
            }
        }
    }

    if (toupgrade != versions::HTTP_v1_1) {
        http::response resp (this->request_data, 101, *config);
        resp.header(HEADER.CONNECTION, "upgrade");
        switch (toupgrade) {
            case versions::HTTP_v2:
                resp.header(HEADER.UPGRADE, "h2c");
            break;
            default:
                break;
        }

        co_await send_response(resp);
    }

    co_return toupgrade;
}
