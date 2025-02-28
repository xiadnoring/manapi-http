#include "http/HeaderView.hpp"

#include <http/HTTPv2.hpp>

#include "ManapiUnicode.hpp"
#include "ManapiUtils.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "crypto/ManapiURL.hpp"
#include "http/base_http.hpp"
#include "http/HTTPv1_1.hpp"

manapi::net::http::HeaderView::HeaderView(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site)
    : worker(std::move(worker)), config(std::move(config)), site(site) {
}

manapi::net::http::HeaderView::HeaderView(std::shared_ptr<worker::connection> connection, std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site)
    : worker(std::move(worker)), config(std::move(config)), site(site) {
    this->connection = std::move(connection);

    this->buffer = this->site.bufferpool().get();
}

manapi::net::http::HeaderView::~HeaderView() {}

manapi::future<void> manapi::net::http::HeaderView::doit() {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    bool upgraded = false;

    this->buffer->resize(this->config->buffer_size());
    this->parse_vars = parse_vars_t{};

    auto &d = this->parse_vars.value();

    if (co_await this->worker->configure_connection(this->connection)) {
        while (true) {

            this->request_data.path = {};
            this->request_data.divided = -1;

            ssize_t size = 0, j = 0;
            this->current = [this](char & PH1) { this->_parse_method(std::forward<decltype(PH1)>(PH1)); };
            while (!d.finished) {
                size = co_await this->worker->read (*this->connection, this->buffer->data(), static_cast<ssize_t>(this->buffer->size()));
                if (size <= 0) { break; }
                for (j = 0; j < size && !d.finished; j++) {
                    this->current (this->buffer->at(j));
                }
            }

            if (std::exchange(upgraded, false)) {

            }
            else {
                if (!d.finished) {
                    break;
                }

                const auto version = http::config::parse_http_version(this->request_data.http.substr(5));
                this->connection->version = version;
            }

            d.buffer = {};
            /* clean up */
            this->parse_vars.reset();

            switch (this->connection->version) {
                case versions::HTTP_v1_1: {
                    auto client = http::http_v1_1::create (this->worker, this->config, this->site);
                    client->request_data = std::move(this->request_data);
                    client->connection = this->connection;
                    client->buffer = std::move(this->buffer);
                    client->prepare();
                    co_await client->parse_request(j, size);
                    co_await client->execute_handler();

                    if (client->connection_was_upgraded()) {
                        this->connection->version = client->get_upgraded_version();
                        this->buffer = std::move(client->buffer);

                        d.finished = false;
                        upgraded = true;
                        continue;
                    }

                    break;
                }
                case versions::HTTP_v2: {
                    auto client = http::http_v2::create(this->worker, this->config, this->site);
                    client->connection = this->connection;
                    client->start = start;
                    client->buffer = std::move(this->buffer);

                    http::request_data_clear(this->request_data);
                    co_await client->parse_request(j, size);
                    long cnt = client.use_count();
                    if (cnt != 1) {
                        //MANAPIHTTP_LOG("Possible bug: http2.use_count() != 1 ({})", cnt);
                    }
                    break;
                }
                default:
                    http::request_data_clear(this->request_data);
                break;
            }

            break;
        }
    }


    co_await this->worker->connection_close(this->connection, false);

    co_return;
}

void manapi::net::http::HeaderView::_parse_method(char &c) {
    if (!std::isalpha(c)) {
        // if (!methods.contains(parse_vars.buffer)) {
        //     THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "Invalid method: {}", parse_vars.buffer);
        // }

        this->request_data.method = std::move(this->parse_vars.value().buffer);

        this->current = [this](char & PH1) { this->_skip_white_space(std::forward<decltype(PH1)>(PH1)); };
        this->next = [this](char & PH1) { this->_parse_uri(std::forward<decltype(PH1)>(PH1)); };
        this->current(c);
        return;
    }

    this->parse_vars.value().buffer += c;
}

void manapi::net::http::HeaderView::_skip_white_space(char &c) {
    if (c == ' ') {
        this->current = this->next;
        return;
    }
    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid char");
}

void manapi::net::http::HeaderView::_next_line(char &c) {
    auto &d = this->parse_vars.value();
    if (d.next_line_state) {
        d.next_line_state = false;
        if (c == '\n') {
            //current = next;
            d.finished = true;
            return;
        }
    }
    else {
        if (c == '\r') {
            d.next_line_state = true;
            return;
        }
    }

    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid symbol");
}

void manapi::net::http::HeaderView::_parse_uri(char &c) {
    auto &d = this->parse_vars.value();
    
    if (c == ' ') {
        this->_cleanup_uri();
        this->current = [this](char & PH1) { this->_skip_white_space(std::forward<decltype(PH1)>(PH1)); };
        this->next = [this](char & PH1) { this->_parse_http(std::forward<decltype(PH1)>(PH1)); };
        this->current(c);
        return;
    }

    if (!crypto::url_allowed_symbol(c)) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid char");
    }

    this->request_data.uri += c;

    if (d.hex_index >= 0) {
        d.hex_symbols[d.hex_index] = c;

        if (d.hex_index == 1) {
            char x = static_cast<char> (manapi::unicode::hex2dec(d.hex_symbols[0]) << 4 | manapi::unicode::hex2dec(
                                 d.hex_symbols[1]));

            if (((d.hex_symbols[0] >= 'a' && d.hex_symbols[0] <= 'z') || (d.hex_symbols[0] >= 'A' && d.hex_symbols[0] <= 'Z')
                || (d.hex_symbols[0] >= '0' && d.hex_symbols[0] <= '9')) && ((d.hex_symbols[1] >= 'a' && d.hex_symbols[1] <= 'z') || (d.hex_symbols[1] >= 'A' && d.hex_symbols[1] <= 'Z')
                || (d.hex_symbols[1] >= '0' && d.hex_symbols[1] <= '9'))) {
                this->request_data.path.back() += x;
            } else {
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

void manapi::net::http::HeaderView::_cleanup_uri() {
    if (this->request_data.divided!=-1) { return; }
    for (auto i = static_cast<ssize_t>(this->request_data.path.size() - 1); i >= 0; i--) {
        if (this->request_data.path[i].empty()) { this->request_data.path.pop_back(); }
        else { break; }
    }
}

void manapi::net::http::HeaderView::_parse_http(char &c) {
    if (unicode::is_space_symbol(c)) {
        this->current = [this](char & PH1) { this->_next_line(std::forward<decltype(PH1)>(PH1)); };

        this->current (c);
        return;
    }

    this->request_data.http += c;
}