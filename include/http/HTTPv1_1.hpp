#ifndef MANAPIHTTP_HTTP_HTTPV1_1_HPP
#define MANAPIHTTP_HTTP_HTTPV1_1_HPP

#include "./Base.hpp"
#include "../ManapiHttpConfig.hpp"

namespace manapi::net::http {
    class http_v1_1 : public http::base
    {
    public:
        http_v1_1 (std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        ~http_v1_1 () override;
        static std::shared_ptr<http_v1_1> create (std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        void doit() override;
        manapi::future<void> parse_request(ssize_t j, ssize_t size) override;
        manapi::future<void> execute_handler () override;

        [[nodiscard]] bool connection_was_upgraded () const;
        [[nodiscard]] versions::http get_upgraded_version () const;
    protected:
        void _skip_white_space (char &c);
        void _next_line (char &c);
        void _parse_headers (char &c);

        future<versions::http> upgrade_connection ();

        struct parse_vars_t {
            // states
            std::string buffer;

            bool next_line_state = false;
            char hex_symbols[2];
            char hex_index = -1;

            // Headers
            std::string key;
            std::string *value;

            bool is_key = true;
            bool dbl = false;

            bool finished = false;
        } parse_vars;

        versions::http upgraded = versions::HTTP_v1_1;
    };
}

#endif //MANAPIHTTP_HTTP_HTTPV1_1_HPP
