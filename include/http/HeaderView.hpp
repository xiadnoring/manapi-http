#ifndef MANAPIHTTP_HTTP_HEADERVIEW_HPP
#define MANAPIHTTP_HTTP_HEADERVIEW_HPP

#include <string>
#include <functional>

#include "../ManapiHttpConfig.hpp"
#include "../ManapiSite.hpp"
#include "../ManapiUtils.hpp"
#include "../worker/Base.hpp"

namespace manapi::net::http {
    class HeaderView {
    public:
        HeaderView (std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        HeaderView (std::shared_ptr<worker::connection> connection, std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        ~HeaderView();

        future<void> doit ();

        std::shared_ptr <worker::connection> connection;
    private:
        void _skip_white_space (char &c);
        void _next_line (char &c);
        void _parse_method (char &c);
        void _parse_uri (char &c);
        void _cleanup_uri ();
        void _parse_http (char &c);

        std::function<void(char&)> current, next;
        struct parse_vars_t {
            // states
            std::string buffer;

            bool next_line_state = false;
            char hex_symbols[2];
            char hex_index = -1;

            bool finished = false;
        } parse_vars;

        http::request_data_t request_data;
        std::shared_ptr<manapi::net::worker::base> worker;
        std::shared_ptr<manapi::net::http::config> config;
        manapi::net::site &site;
        std::string buffer;
    };
}

#endif //MANAPIHTTP_HTTP_HEADERVIEW_HPP
