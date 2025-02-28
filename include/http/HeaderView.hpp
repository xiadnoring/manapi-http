#pragma once

#include <string>
#include <functional>

#include "../components/Buffer.hpp"
#include "../ManapiUtils.hpp"
#include "base_http.hpp"
#include "../ManapiHttpConfig.hpp"
#include "../ManapiSite.hpp"
#include "../ManapiUtils.hpp"

namespace manapi::net::http {

    class HeaderView {
        struct parse_vars_t {
            // states
            std::string buffer;

            bool next_line_state = false;
            char hex_symbols[2];
            char hex_index = -1;

            bool finished = false;
        };
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
        std::optional<parse_vars_t> parse_vars;

        http::request_data_t request_data;
        std::shared_ptr<manapi::net::worker::base> worker;
        std::shared_ptr<manapi::net::http::config> config;
        manapi::net::site &site;
        object_item_pool<bytebuffer, std::size_t> buffer{};
    };
}
