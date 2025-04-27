#include "http/HeaderView.hpp"

#include <http/HTTPv2.hpp>

#include "encoding/ManapiUnicode.hpp"
#include "ManapiUtils.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "encoding/ManapiURL.hpp"
#include "http/base_http.hpp"
#include "http/HTTPv1_1.hpp"

enum header_view_flags {
    HEADER_VIEW_FLAG_UPGRADED = 0b1
};

manapi::net::http::HeaderView::HeaderView(std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site)
    : worker(std::move(worker)), config(std::move(config)), site(site), parse_vars(nullptr), request_data({}) {
}

manapi::net::http::HeaderView::HeaderView(std::shared_ptr<worker::connection> connection, std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site)
    : worker(std::move(worker)), config(std::move(config)), site(site), request_data({}) {
    this->connection = std::move(connection);
    this->parse_vars = nullptr;
    this->buffer = this->site.bufferpool()->get();
}

manapi::net::http::HeaderView::~HeaderView() = default;

manapi::future<void> manapi::net::http::HeaderView::doit() {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    int flags = 0;

    this->buffer->resize(this->config->buffer_size());


    if (co_await this->worker->configure_connection(this->connection)) {
        while (true) {
            this->parse_vars = std::make_shared<parse_vars_t>();
            auto &d = *this->parse_vars;

            this->request_data.path = {};
            this->request_data.divided = -1;

            ssize_t size = 0, j = 0;
            bool error = false;
            this->current = [this](char & PH1) { this->_parse_method(std::forward<decltype(PH1)>(PH1)); };
            while (!d.finished) {
                size = co_await this->worker->read (*this->connection, this->buffer->data(), static_cast<ssize_t>(this->buffer->size()));
                if (size < 0) {
                    error = true;
                    break;
                }
                if (size == 0) {
                    break;
                }
                for (j = 0; j < size && !d.finished; j++) {
                    this->current (this->buffer->at(j));
                }
            }

            if (error) {
                break;
            }

            if (flags & HEADER_VIEW_FLAG_UPGRADED) {
                flags ^= HEADER_VIEW_FLAG_UPGRADED;
            }
            else {
                if (!d.finished) {
                    break;
                }

                const auto version = http::config::parse_http_version(this->parse_vars->http.substr(5));
                this->parse_vars->http = {};
                this->connection->version = version;
                this->request_data.http = this->connection->version;
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

                    if (co_await client->parse_request(j, size)) {
                        if (client->connection_was_upgraded()) {
                            if (client->buffer) {
                                this->buffer = std::move(client->buffer);
                            }
                            else if (client->request_data.buffer) {
                                this->buffer = std::move(client->request_data.buffer);
                            }
                            else {
                                THROW_MANAPIHTTP_EXCEPTION2 (ERR_BUG, "the buffer was wasted. subsequent execution will return a fatal error.");
                            }

                            this->connection->version = client->upgraded_version();

                            d.finished = false;
                            flags |= HEADER_VIEW_FLAG_UPGRADED;
                            continue;
                        }

                        co_await client->execute_handler();
                    }

                    break;

                    break;
                }
                case versions::HTTP_v2: {
                    auto client = http::http_v2::create(dynamic_pointer_cast<worker::TCP>(this->worker), this->config, this->site);
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

    co_return;
}

void manapi::net::http::HeaderView::_parse_method(char &c) {
    if (!std::isalpha(c)) {
        // if (!methods.contains(parse_vars.buffer)) {
        //     THROW_MANAPIHTTP_EXCEPTION(ERR_HTTP_PROTOCOL_ERROR, "Invalid method: {}", parse_vars.buffer);
        // }

        this->request_data.method = std::move(this->parse_vars->buffer);

        this->current = [this](char & PH1) { this->_skip_white_space(std::forward<decltype(PH1)>(PH1)); };
        this->next = [this](char & PH1) { this->_parse_uri(std::forward<decltype(PH1)>(PH1)); };
        this->current(c);
        return;
    }

    this->parse_vars->buffer += c;
}

void manapi::net::http::HeaderView::_skip_white_space(char &c) {
    if (c == ' ') {
        this->current = this->next;
        return;
    }
    THROW_MANAPIHTTP_EXCEPTION2(ERR_HTTP_PROTOCOL_ERROR, "Invalid char");
}

void manapi::net::http::HeaderView::_next_line(char &c) {
    auto &d = *this->parse_vars;
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
    if (c == ' ') {
        auto data = this->parse_vars->url_decode.result();
        this->request_data.path = std::move(data.first);
        this->request_data.divided = data.second;

        this->current = [this](char & PH1) { this->_skip_white_space(std::forward<decltype(PH1)>(PH1)); };
        this->next = [this](char & PH1) { this->_parse_http(std::forward<decltype(PH1)>(PH1)); };
        this->current(c);

        return;
    }

    this->parse_vars->url_decode << c;
}

void manapi::net::http::HeaderView::_parse_http(char &c) {
    if (unicode::is_space_symbol(c)) {
        this->current = [this](char & PH1) { this->_next_line(std::forward<decltype(PH1)>(PH1)); };

        this->current (c);
        return;
    }

    this->parse_vars->http.push_back(c);
}