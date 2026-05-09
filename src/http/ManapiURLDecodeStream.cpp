#include <utility>

#include "ManapiDebug.hpp"
#include "http/ManapiURLDecodeStream.hpp"
#include "encoding/ManapiURL.hpp"
#include "encoding/ManapiUnicode.hpp"
#include "../include/ManapiUtils.hpp"

manapi::net::http::url_decode_stream::url_decode_stream() : hex_symbols{'\0','\0'} {
    this->hex_index = -1;
    this->divided_ = -1;
}

manapi::net::http::url_decode_stream::~url_decode_stream() = default;

int manapi::net::http::url_decode_stream::operator<<(const char &c) {
    return this->handle_char_(c);
}

int manapi::net::http::url_decode_stream::operator<<(std::string_view data) {
    for (auto const &c : data) {
        if (auto rhs = this->handle_char_(c)) {
            return rhs;
        }
    }
    return 0;
}

std::vector<std::string> manapi::net::http::url_decode_stream::result() {
    if (this->hex_index != -1)
        throw manapi::exception (ERR_INVALID_ARGUMENT, "url_decode_stream:failed");
    this->cleanup_uri_();
    return std::move(this->result_);
}

int manapi::net::http::url_decode_stream::divided() {
    return std::exchange(this->divided_, -1);
}

int manapi::net::http::url_decode_stream::handle_char_(const char &c) {
    if (!encoding::url_allowed_symbol(c)) {
        return -1;
    }

    if (this->divided_ == -1) {
        if (this->hex_index >= 0) {
            this->hex_symbols[this->hex_index] = c;

            if (this->hex_index == 1) {
                char x = static_cast<char> (manapi::unicode::onehex2dec(static_cast<uint8_t>(this->hex_symbols[0])) << 4 | manapi::unicode::onehex2dec(
                                     static_cast<uint8_t>(this->hex_symbols[1])));

                if (isalnum(this->hex_symbols[0]) && isalnum(this->hex_symbols[1])) {
                    this->result_.back() += x;
                }
                else {
                    this->result_.back() += '%';
                    this->result_.back() += this->hex_symbols;
                }

                this->hex_index = -1;

                return 0;
            }

            this->hex_index++;

            return 0;
        }

        if (c == '%' && !this->result_.empty()) {
            this->hex_index = 0;

            return 0;
        }

        if (c == '/') {
            if (this->result_.empty() || !this->result_.back().empty()) {
                this->result_.emplace_back("");
            }
            return 0;
        }

        if (c == '?' || c == '#') {
            this->cleanup_uri_ ();
            this->divided_ = static_cast<int>(this->result_.size());
            this->result_.emplace_back(std::string{c});
            return 0;
        }
    }

    if (!this->result_.empty()) {
        this->result_.back().push_back(c);
    }
    return 0;
}

void manapi::net::http::url_decode_stream::cleanup_uri_() {
    if (this->divided_ != -1) { return; }
    std::size_t i = this->result_.size();
    while (i) {
        i--;

        if (this->result_.operator[](i).empty()) {
            this->result_.pop_back();
        }
        else {
            break;
        }
    }
}