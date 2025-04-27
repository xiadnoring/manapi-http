#ifndef MANAPIHTTP_HTTP_HTTPV2_HPP
#define MANAPIHTTP_HTTP_HTTPV2_HPP

#include <thread>

#include "../ManapiUtils.hpp"
#include "./base_http.hpp"
#include "../worker/HTTPv2.hpp"

namespace manapi::net::http {
    class http_v2 : public http::base {
    public:
        http_v2 (std::shared_ptr<manapi::net::worker::base> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);

        static std::shared_ptr<worker::http_v2> create (std::shared_ptr<manapi::net::worker::TCP> worker, std::shared_ptr<manapi::net::http::config> config, manapi::net::site &site);
        manapi::future<bool> parse_request(ssize_t j, ssize_t size) override;
        manapi::future<void> execute_handler() override;
    };
}

#endif //MANAPIHTTP_HTTP_HTTPV2_HPP
