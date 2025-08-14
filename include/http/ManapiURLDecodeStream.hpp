#pragma once

#include <memory>
#include <string>
#include <vector>

namespace manapi::net::http {
    class url_decode_stream {
    public:
        url_decode_stream();
        ~url_decode_stream();
        /**
         *
         * @param c Input Char
         * @return 0 on succes, <0 on error
         */
        int operator << (const char &c);
        /**
         *
         * @param c Input String
         * @return 0 on succes, <0 on error
         */
        int operator << (std::string_view data);
        std::vector<std::string> result ();
        int divided();
    private:
        int handle_char_ (const char &c);
        void cleanup_uri_ ();

        char hex_symbols[2];
        char hex_index;

        std::vector<std::string> result_;
        int divided_;
    };
}