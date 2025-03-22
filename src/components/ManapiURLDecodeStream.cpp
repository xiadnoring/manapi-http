#include "components/ManapiURLDecodeStream.hpp"

#include <utility>

#include "crypto/ManapiURL.hpp"

#include "ManapiDebug.hpp"
#include "ManapiUnicode.hpp"
#include "ManapiUtils.hpp"

manapi::net::http::url_decode_stream::url_decode_stream() : hex_symbols{'\0','\0'} {
    this->hex_index = -1;
    this->divided = -1;
}

manapi::net::http::url_decode_stream::~url_decode_stream() = default;

void manapi::net::http::url_decode_stream::operator<<(const char &c) {
    this->handle_char_(c);
}

void manapi::net::http::url_decode_stream::operator<<(std::string_view data) {
    for (auto &c : data) {
        handle_char_(c);
    }
}

std::pair<std::vector<std::string>, ssize_t> manapi::net::http::url_decode_stream::result() {
    if (this->hex_index != -1) {
        THROW_MANAPIHTTP_EXCEPTION2 (ERR_PARSE_UNEXPECTED_END, "this->hex_index != -1");
    }
    this->cleanup_uri_();
    return std::make_pair(std::move(this->result_), std::exchange(this->divided, -1));
}

void manapi::net::http::url_decode_stream::handle_char_(const char &c) {
    if (!crypto::url_allowed_symbol(c)) {
        THROW_MANAPIHTTP_EXCEPTION2(ERR_PARSE_INVALID_CHAR, "url_decode_stream: invalid char");
    }

    if (this->hex_index >= 0) {
        this->hex_symbols[this->hex_index] = c;

        if (this->hex_index == 1) {
            char x = static_cast<char> (manapi::unicode::hex2dec(this->hex_symbols[0]) << 4 | manapi::unicode::hex2dec(
                                 this->hex_symbols[1]));

            if (((this->hex_symbols[0] >= 'a' && this->hex_symbols[0] <= 'z') || (this->hex_symbols[0] >= 'A' && this->hex_symbols[0] <= 'Z')
                || (this->hex_symbols[0] >= '0' && this->hex_symbols[0] <= '9')) && ((this->hex_symbols[1] >= 'a' && this->hex_symbols[1] <= 'z') || (this->hex_symbols[1] >= 'A' && this->hex_symbols[1] <= 'Z')
                || (this->hex_symbols[1] >= '0' && this->hex_symbols[1] <= '9'))) {
                this->result_.back() += x;
            }
            else {
                this->result_.back() += '%';
                this->result_.back() += this->hex_symbols;
            }

            this->hex_index = -1;

            return;
        }

        this->hex_index++;

        return;
    }

    if (c == '%' && !this->result_.empty()) {
        this->hex_index = 0;

        return;
    }

    if (this->divided == -1) {
        if (c == '/') {
            if (this->result_.empty() || !this->result_.back().empty()) {
                this->result_.emplace_back("");
            }
            return;
        }

        if (c == '?' || c == '#') {
            this->cleanup_uri_ ();
            this->divided = static_cast<ssize_t>(this->result_.size());
            this->result_.emplace_back(std::string{c});
            return;
        }
    }

    if (!this->result_.empty()) {
        this->result_.back().push_back(c);
    }
}

void manapi::net::http::url_decode_stream::cleanup_uri_() {
    if (this->divided!=-1) { return; }
    for (ssize_t i = static_cast<ssize_t>(this->result_.size()) - 1; i >= 0; i--) {
        if (this->result_[i].empty()) {
            this->result_.pop_back();
        }
        else {
            break;
        }
    }
}