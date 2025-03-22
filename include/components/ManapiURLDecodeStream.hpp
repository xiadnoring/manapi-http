#pragma once

#include <string>
#include <vector>

namespace manapi::net::http {
    class url_decode_stream {
    public:
        url_decode_stream();
        ~url_decode_stream();
        void operator << (const char &c);
        void operator << (std::string_view data);
        std::pair<std::vector<std::string>, ssize_t> result ();
    private:
        void handle_char_ (const char &c);
        void cleanup_uri_ ();

        char hex_symbols[2];
        char hex_index;

        std::vector<std::string> result_;
        ssize_t divided;
    };
}