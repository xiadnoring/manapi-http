#include "http/HTTPv2.hpp"

manapi::net::http::http_v2::http_v2(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config,
    manapi::net::site &site) : base(std::move(worker), std::move(config), site) {

}

std::shared_ptr<manapi::net::worker::http_v2> manapi::net::http::http_v2::create(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config,manapi::net::site &site) {
    std::shared_ptr<manapi::net::worker::http_v2> w = std::make_shared <manapi::net::worker::http_v2> (std::move(worker), std::move(config), site);
    w->new_dependency = [w = std::weak_ptr<manapi::net::worker::http_v2> (w)] () {
        return std::shared_ptr<manapi::net::worker::http_v2> (w);
    };
    return std::move(w);
}

manapi::future<void> manapi::net::http::http_v2::parse_request(ssize_t j, ssize_t size) {
    for (char & i : request_data.uri) {
        _parse_uri(i);
    }
    this->_cleanup_uri();
    co_return;
}

manapi::future<void> manapi::net::http::http_v2::execute_handler() {
    const auto handler = site.get_handler(request_data);
    co_await handle_request(&handler, request_data);
    co_return;
}

void manapi::net::http::http_v2::_parse_uri(char &c) {
    if (c == ' ') {
        if (this->parse_vars.uri_finished) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "URI parse was finished with error"); }
        this->parse_vars.uri_finished = true;
        return;
    }

    if (!utils::uri_allowed_symbol(c)) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid char");
    }

    if (parse_vars.hex_index >= 0) {
        parse_vars.hex_symbols[parse_vars.hex_index] = c;

        if (parse_vars.hex_index == 1) {
            char x = static_cast<char> (manapi::net::utils::hex2dec(parse_vars.hex_symbols[0]) << 4 | manapi::net::utils::hex2dec(
                                 parse_vars.hex_symbols[1]));

            if (((parse_vars.hex_symbols[0] >= 'a' && parse_vars.hex_symbols[0] <= 'z') || (parse_vars.hex_symbols[0] >= 'A' && parse_vars.hex_symbols[0] <= 'Z')
                || (parse_vars.hex_symbols[0] >= '0' && parse_vars.hex_symbols[0] <= '9')) && ((parse_vars.hex_symbols[1] >= 'a' && parse_vars.hex_symbols[1] <= 'z') || (parse_vars.hex_symbols[1] >= 'A' && parse_vars.hex_symbols[1] <= 'Z')
                || (parse_vars.hex_symbols[1] >= '0' && parse_vars.hex_symbols[1] <= '9'))) {
                request_data.path.back() += x;
            }
            else {
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

void manapi::net::http::http_v2::_cleanup_uri() {
    if (request_data.divided!=-1) { return; }
    for (ssize_t i = request_data.path.size() - 1; i >= 0; i--) {
        if (request_data.path[i].empty()) {
            request_data.path.pop_back();
        }
        else {
            break;
        }
    }
}
