#include "http/HTTPv2.hpp"

#include "ManapiUnicode.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "crypto/ManapiURL.hpp"

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
    this->parse_vars = parse_vars_t{};

    for (char & i : this->request_data.uri) {
        _parse_uri(i);
    }
    this->_cleanup_uri();
    this->parse_vars.reset();
    co_return;
}

manapi::future<void> manapi::net::http::http_v2::execute_handler() {
    const auto handler = this->site.get_handler(this->request_data);
    co_await handle_request(&handler, this->request_data);
    co_return;
}

void manapi::net::http::http_v2::_parse_uri(char &c) {
    auto &d = this->parse_vars.value();
    if (c == ' ') {
        if (d.uri_finished) { THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "URI parse was finished with error"); }
        d.uri_finished = true;
        return;
    }

    if (!crypto::url_allowed_symbol(c)) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid char");
    }

    if (d.hex_index >= 0) {
        d.hex_symbols[d.hex_index] = c;

        if (d.hex_index == 1) {
            char x = static_cast<char> (manapi::unicode::hex2dec(d.hex_symbols[0]) << 4 | manapi::unicode::hex2dec(
                                 d.hex_symbols[1]));

            if (((d.hex_symbols[0] >= 'a' && d.hex_symbols[0] <= 'z') || (d.hex_symbols[0] >= 'A' && d.hex_symbols[0] <= 'Z')
                || (d.hex_symbols[0] >= '0' && d.hex_symbols[0] <= '9')) && ((d.hex_symbols[1] >= 'a' && d.hex_symbols[1] <= 'z') || (d.hex_symbols[1] >= 'A' && d.hex_symbols[1] <= 'Z')
                || (d.hex_symbols[1] >= '0' && d.hex_symbols[1] <= '9'))) {
                this->request_data.path.back() += x;
            }
            else {
                this->request_data.path.back() += '%';
                this->request_data.path.back() += d.hex_symbols;
            }

            d.hex_index = -1;

            return;
        }

        d.hex_index++;

        return;
    }

    if (c == '%' && !this->request_data.path.empty()) {
        d.hex_index = 0;

        return;
    }

    if (this->request_data.divided == -1) {
        if (c == '/') {
            if (this->request_data.path.empty() || !this->request_data.path.back().empty()) {
                this->request_data.path.emplace_back("");
            }
            return;
        }

        if (c == '?') {
            this->_cleanup_uri ();
            this->request_data.divided = static_cast<ssize_t>(this->request_data.path.size());
            this->request_data.path.emplace_back("");
            return;
        }
    }

    if (!this->request_data.path.empty()) {
        this->request_data.path.back() += c;
    }
}

void manapi::net::http::http_v2::_cleanup_uri() {
    if (this->request_data.divided!=-1) { return; }
    for (ssize_t i = this->request_data.path.size() - 1; i >= 0; i--) {
        if (this->request_data.path[i].empty()) {
            this->request_data.path.pop_back();
        }
        else {
            break;
        }
    }
}
