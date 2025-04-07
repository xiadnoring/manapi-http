#ifndef MANAPIHTTP_HTTP_HTTPV1_1_HPP
#define MANAPIHTTP_HTTP_HTTPV1_1_HPP

#include "../ManapiUtils.hpp"
#include "./base_http.hpp"
#include "../ManapiHttpConfig.hpp"

namespace manapi::net::http {
    class http_v1_1 : public http::base
    {
    public:
        enum available_flags {
            FLAG_UPGRADED_BY_SERVER = 0b1
        };

        http_v1_1 (std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        ~http_v1_1 () override;
        static std::shared_ptr<http_v1_1> create (std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        void doit() override;
        manapi::future<bool> parse_request(ssize_t j, ssize_t size) override;
        manapi::future<void> execute_handler () override;

        [[nodiscard]] bool connection_was_upgraded () const;
        [[nodiscard]] int upgraded_version () const;
        future<ssize_t> read (void *buffer, ssize_t size) override;
        [[nodiscard]] int flags () const;
    protected:
        future<bool> validate_http_version() override;
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

        int flags_ = 0;
        int upgraded = versions::HTTP_v1_1;
        std::move_only_function<manapi::future<ssize_t>(void *buffer, ssize_t size)> read_async;
    };
}

#endif //MANAPIHTTP_HTTP_HTTPV1_1_HPP
