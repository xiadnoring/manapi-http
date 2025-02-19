#include "http/HTTPv1_1.hpp"

#include "ManapiFilesystem.hpp"
#include "services/ManapiFetch.hpp"
#include "http/HeaderView.hpp"

manapi::net::http::http_v1_1::http_v1_1(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site) : base(std::move(worker), std::move(config), site) {
    this->buffer_size = 0;
}

manapi::net::http::http_v1_1::~http_v1_1() = default;

std::shared_ptr<manapi::net::http::http_v1_1> manapi::net::http::http_v1_1::create(
    std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config,
    manapi::net::site &site) {
    return std::make_shared<manapi::net::http::http_v1_1>(std::move(worker), std::move(config), site);
}

void manapi::net::http::http_v1_1::doit() {

}

manapi::future<void> manapi::net::http::http_v1_1::parse_request(ssize_t j, ssize_t size) {
    this->request_data.body_index = 0;


    do {
        std::size_t s = this->config->buffer_size().load();
        if (this->buffer_size != s) {
            auto p = this->buffer.release();

            try {
                this->buffer.reset(manapi::memory::realloc(p, s));
                this->buffer_size = s;
            }
            catch (...) {
                this->buffer.reset(p);
                std::rethrow_exception(std::current_exception());
            }
        }
    }
    while (false);

    {
        if (size == 0) {
            THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "HTTP Status hasn't been parsed");
        }

        this->current = [this](auto && PH1) { _parse_headers(std::forward<decltype(PH1)>(PH1)); };
        goto skip;

        while (!this->parse_vars.finished) {
            size = co_await this->worker->read (*this->connection, this->buffer.get(), static_cast<ssize_t>(this->buffer_size));
            if (size <= 0) { break; }
            j = 0;
            skip: for (; j < size && !this->parse_vars.finished; j++) {
                this->current (*(this->buffer.get() + j));
            }
        }

        j++;
    }

    ssize_t content_length = 0;
    if (this->request_data.headers.contains(HTTP_HEADER.CONTENT_LENGTH)) {
        content_length = std::stoll(this->request_data.headers[HTTP_HEADER.CONTENT_LENGTH]);
    }

    this->request_data.has_body = content_length > 0;

    if (this->request_data.has_body) {
        this->request_data.body_size = content_length;
        this->request_data.body_left = this->request_data.body_size;
        this->request_data.buffer = std::move(this->buffer);
        this->request_data.buffer_size = std::exchange(this->buffer_size, 0);

        co_await expect_header();
        while (size <= j) {
            size = co_await this->read(this->request_data.buffer.get(), static_cast<ssize_t>(this->request_data.buffer_size));
            if (size < 0) {
                THROW_MANAPIHTTP_EXCEPTION (ERR_HTTP_PROTOCOL_ERROR, "this->read(...) = {}", size);
            }
            this->request_data.body_part = size;
            this->request_data.headers_part = 0;
            if (j > size) {
                j -= size;
            }
            else {
                j = 0;
            }
        }
        this->request_data.headers_part = j;
        this->request_data.body_part = size - this->request_data.headers_part;
        this->request_data.body_index = j;
    }
    else {
        this->request_data.body_index = 0;
        this->request_data.body_size = 0;
    }

    co_return;
}

manapi::future<void> manapi::net::http::http_v1_1::execute_handler() {
    this->upgraded = co_await upgrade_connection();
    if (this->connection_was_upgraded()) {
        co_return;
    }

    const auto handler = this->site.get_handler(this->request_data);
    co_await handle_request(&handler, this->request_data);
    co_return;
}

bool manapi::net::http::http_v1_1::connection_was_upgraded() const {
    return this->upgraded != versions::HTTP_v1_1;
}

manapi::net::http::versions::http manapi::net::http::http_v1_1::get_upgraded_version() const {
    return this->upgraded;
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

    if (parse_vars.dbl) {
        parse_vars.dbl = false;
    }

    if (c == ':' && parse_vars.is_key) {
        parse_vars.is_key = false;
        parse_vars.value = &request_data.headers[parse_vars.key];

        return;
    }



    if (parse_vars.is_key) {
        parse_vars.key += static_cast<char>(std::tolower(c));
    } else {
        if (c == ' ' && parse_vars.value->empty()) {
            return;
        }
        if (!parse_vars.value) {
            THROW_MANAPIHTTP_EXCEPTION2 (ERR_HTTP_PROTOCOL_ERROR, "error in the headers section");
        }
        *parse_vars.value += c;
    }
}

manapi::future<manapi::net::http::versions::http> manapi::net::http::http_v1_1::upgrade_connection() {
    http::versions::http toupgrade = versions::HTTP_v1_1;

    if (request_data.headers.contains(HTTP_HEADER.CONNECTION)) {
        const auto connection_header = http::parse_header_value(request_data.headers[HTTP_HEADER.CONNECTION]);
        for (const auto &param: connection_header) {
            if (param.value == "Upgrade") {
                if (request_data.headers[HTTP_HEADER.UPGRADE] == "h2c") {
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
        http_response resp (request_data, 101, HTTP_STATUS.SWITCHING_PROTOCOLS_101, *config);
        resp.set_header(HTTP_HEADER.CONNECTION, "upgrade");
        switch (toupgrade) {
            case versions::HTTP_v2:
                resp.set_header(HTTP_HEADER.UPGRADE, "h2c");
            break;
            default:
                break;
        }

        co_await send_response(resp);
    }

    co_return toupgrade;
}
