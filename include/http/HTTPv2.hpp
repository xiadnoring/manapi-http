#ifndef MANAPIHTTP_HTTP_HTTPV2_HPP
#define MANAPIHTTP_HTTP_HTTPV2_HPP

#include <thread>

#include "./http/Base.hpp"
#include "../worker/HTTPv2.hpp"

namespace manapi::net::http {
    class http_v2 : public http::base {
    public:
        http_v2 (std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);

        static std::shared_ptr<worker::http_v2> create (std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        void parse_request(ssize_t j, ssize_t size) override;
        void execute_handler() override;
    private:
        void _parse_uri (char &c);
        void _cleanup_uri ();

        struct parse_vars_t {
            char hex_symbols[2];
            char hex_index = -1;
            bool uri_finished = false;
        } parse_vars;
    };
}

#endif //MANAPIHTTP_HTTP_HTTPV2_HPP
