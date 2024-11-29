#include "http/HeaderView.hpp"

#include <http/HTTPv2.hpp>

#include "ManapiUtils.hpp"
#include "http/Base.hpp"
#include "http/HTTPv1_1.hpp"

manapi::net::http::HeaderView::HeaderView(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site)
    : worker(std::move(worker)), config(std::move(config)), site(site) {
    this->connection = base::create_connection(this->worker);
}

manapi::net::http::HeaderView::HeaderView(std::shared_ptr<worker::connection> connection, std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site)
    : worker(std::move(worker)), config(std::move(config)), site(site) {
    this->connection = std::move(connection);
}

manapi::net::http::HeaderView::~HeaderView() {
}

void manapi::net::http::HeaderView::doit() {
    worker->configure_connection(*connection);

    request_data.path = {};
    request_data.divided = -1;

    ssize_t size = 0, j = 0;
    buffer.resize(16384);
    current = std::bind(&HeaderView::_parse_method, this, std::placeholders::_1);
    while (!parse_vars.finished) {
        size = worker->read (*connection, buffer.data(), buffer.size());
        if (size <= 0) { break; }
        for (j = 0; j < size && !parse_vars.finished; j++) {
            current (buffer.at(j));
        }
    }

    // ghost
    if (!parse_vars.finished) { return; }

    const auto version = http::config::parse_http_version(request_data.http.substr(5));
    connection->version = version;
    std::cout<<request_data.http << "\n";
    switch (connection->version) {
        case versions::HTTP_v1_1: {
            http::http_v1_1 client (worker, config, site);
            client.request_data = std::move(request_data);
            client.connection = connection;
            client.buffer = std::move(buffer);
            client.prepare();
            client.parse_request(j, size);
            client.execute_handler();
        }
        break;
        case versions::HTTP_v2: {
            auto client = worker::http_v2::create(worker, config, site);
            client->connection = connection;
            client->buffer = std::move(buffer);
            client->parse_request(j, size);
        }
        break;
    }
}

void manapi::net::http::HeaderView::_parse_method(char &c) {
    if (!std::isalpha(c)) {
        // if (!methods.contains(parse_vars.buffer)) {
        //     THROW_MANAPI_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "Invalid method: {}", parse_vars.buffer);
        // }

        request_data.method = std::move(parse_vars.buffer);

        current = std::bind(&HeaderView::_skip_white_space, this, std::placeholders::_1);
        next = std::bind(&HeaderView::_parse_uri, this, std::placeholders::_1);
        current(c);
        return;
    }

    parse_vars.buffer += c;
}

void manapi::net::http::HeaderView::_skip_white_space(char &c) {
    if (c == ' ') {
        current = next;
        return;
    }
    THROW_MANAPI_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid char");
}

void manapi::net::http::HeaderView::_next_line(char &c) {
    if (parse_vars.next_line_state) {
        parse_vars.next_line_state = false;
        if (c == '\n') {
            //current = next;
            parse_vars.finished = true;
            return;
        }
    }
    else {
        if (c == '\r') {
            parse_vars.next_line_state = true;
            return;
        }
    }

    THROW_MANAPI_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid symbol");
}

void manapi::net::http::HeaderView::_parse_uri(char &c) {
    if (c == ' ') {
        this->_cleanup_uri();
        current = std::bind(&HeaderView::_skip_white_space, this, std::placeholders::_1);
        next = std::bind(&HeaderView::_parse_http, this, std::placeholders::_1);
        current(c);
        return;
    }

    if (!utils::uri_allowed_symbol(c)) {
        THROW_MANAPI_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid char");
    }

    request_data.uri += c;

    if (parse_vars.hex_index >= 0) {
        parse_vars.hex_symbols[parse_vars.hex_index] = c;

        if (parse_vars.hex_index == 1) {
            char x = static_cast<char> (manapi::net::utils::hex2dec(parse_vars.hex_symbols[0]) << 4 | manapi::net::utils::hex2dec(
                                 parse_vars.hex_symbols[1]));

            if (true || manapi::net::utils::valid_special_symbol(x)) {
                request_data.path.back() += x;
            } else {
                request_data.path.back() += '%';
                request_data.path.back() += parse_vars.hex_symbols;
            }

            parse_vars.hex_index = -1;

            return;
        }

        parse_vars.hex_index++;

        return;
    }

    if (c == '%' && !request_data.path.empty()) {
        parse_vars.hex_index = 0;

        return;
    }

    if (request_data.divided == -1) {
        if (c == '/') {
            if (request_data.path.empty() || !request_data.path.back().empty()) {
                request_data.path.emplace_back("");
            }
            return;
        }

        if (c == '?') {
            this->_cleanup_uri ();
            request_data.divided = static_cast<ssize_t>(request_data.path.size());
            request_data.path.emplace_back("");
            return;
        }
    }

    if (!request_data.path.empty()) {
        request_data.path.back() += c;
    }
}

void manapi::net::http::HeaderView::_cleanup_uri() {
    if (request_data.divided!=-1) { return; }
    for (ssize_t i = request_data.path.size() - 1; i >= 0; i--) {
        if (request_data.path[i].empty()) { request_data.path.pop_back(); }
        else { break; }
    }
}

void manapi::net::http::HeaderView::_parse_http(char &c) {
    if (utils::is_space_symbol(c)) {
        current = std::bind(&HeaderView::_next_line, this, std::placeholders::_1);

        current (c);
        return;
    }

    request_data.http += c;
}