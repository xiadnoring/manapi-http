#include "http/HTTPv1_1.hpp"

#include "ManapiFilesystem.hpp"
#include "ManapiFetch.hpp"
#include "http/HeaderView.hpp"

manapi::net::http::http_v1_1::http_v1_1(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site) : base(std::move(worker), std::move(config), site) {
    buffer.resize(this->config->get_socket_block_size());
}

manapi::net::http::http_v1_1::~http_v1_1() = default;

void manapi::net::http::http_v1_1::doit() {

}

void manapi::net::http::http_v1_1::parse_request(ssize_t j, ssize_t size) {
    request_data.body_index = 0;

    {
        if (size == 0) {
            THROW_MANAPI_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "HTTP Status hasn't been parsed");
        }

        current = std::bind(&http_v1_1::_parse_headers, this, std::placeholders::_1);
        goto skip;

        while (!parse_vars.finished) {
            size = worker->read (*connection, buffer.data(), buffer.size());
            if (size <= 0) { break; }
            j = 0;
            skip: for (; j < size && !parse_vars.finished; j++) {
                current (buffer.at(j));
            }
        }

        j++;
    }

    size_t content_length = 0;
    if (request_data.headers.contains(HTTP_HEADER.CONTENT_LENGTH)) {
        content_length = std::stoull(request_data.headers[HTTP_HEADER.CONTENT_LENGTH]);
    }

    request_data.has_body = content_length > 0;

    if (request_data.has_body) {
        request_data.buffer = std::move(this->buffer);
        if (size == j) {
            ssize_t rhs = this->read(request_data.buffer.data(), request_data.buffer.size());
            if (rhs < 0) { THROW_MANAPI_EXCEPTION (ERR_HTTP_PROTOCOL_ERROR, "this->read(...) = {}", rhs); }
            request_data.body_part = rhs;
            request_data.headers_part = 0; j = 0;
        }
        else {
            request_data.headers_part = j;
            request_data.body_part = size - request_data.headers_part;
        }
        request_data.body_size = content_length;
        request_data.body_left = request_data.body_size;
        request_data.body_ptr = this->request_data.buffer.data() + j;

        request_data.body_index = 0;
    }
    else {
        request_data.body_ptr = nullptr;
        request_data.body_size = 0;
    }
}

void manapi::net::http::http_v1_1::execute_handler() {
    if (upgrade_connection()) { return; }
    const auto handler = site.get_handler(request_data);
    handle_request(&handler, request_data);
}

void manapi::net::http::http_v1_1::_skip_white_space(char &c) {
    if (c == ' ') {
        current = next;
        return;
    }
    THROW_MANAPI_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid char");
}

void manapi::net::http::http_v1_1::_next_line(char &c) {
    if (parse_vars.next_line_state) {
        parse_vars.next_line_state = false;
        if (c == '\n') {
            current = next;
            return;
        }
    }
    else {
        if (c == '\r') {
            parse_vars.next_line_state = true;
            if (parse_vars.dbl) {
                parse_vars.finished = true;
            }
            return;
        }
    }

    THROW_MANAPI_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid symbol");
}

void manapi::net::http::http_v1_1::_parse_headers(char &c) {
    if (c == '\r') {
        parse_vars.is_key = true;
        if (!parse_vars.key.empty()) {
            parse_vars.key = "";
        }


        current = std::bind(&http_v1_1::_next_line, this, std::placeholders::_1);
        next = std::bind(&http_v1_1::_parse_headers, this, std::placeholders::_1);
        current(c);

        parse_vars.dbl = true;

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
        *parse_vars.value += c;
    }
}

bool manapi::net::http::http_v1_1::upgrade_connection() {
    bool toupgrade = false;

    if (request_data.headers.contains(HTTP_HEADER.CONNECTION)) {
        const auto connection_header = utils::parse_header_value(request_data.headers[HTTP_HEADER.CONNECTION]);
        for (const auto &param: connection_header) {
            if (param.value == "Upgrade") {
                if (request_data.headers[HTTP_HEADER.UPGRADE] == "h2c") {
                    toupgrade = true;
                }
                continue;
            }
            if (param.value == "HTTP2-Settings") {
                //http2settings = request_data.headers["http2-settings"];
                continue;
            }
        }
    }

    if (toupgrade) {
        http_response resp (request_data, 101, HTTP_STATUS.SWITCHING_PROTOCOLS_101, std::make_unique<api::pool> (site.get_tasks_pool().get()), *config);
        resp.set_header(HTTP_HEADER.CONNECTION, "upgrade");
        resp.set_header(HTTP_HEADER.UPGRADE, "h2c");
        send_response(resp);

        std::unique_ptr<HeaderView> hw = std::make_unique<HeaderView>(connection, worker, config, site);
        site.append_task(std::move(hw), 1);
    }

    return toupgrade;
}
