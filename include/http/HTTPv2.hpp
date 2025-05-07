#ifndef MANAPIHTTP_HTTP_HTTPV2_HPP
#define MANAPIHTTP_HTTP_HTTPV2_HPP

#include <thread>

#include "../ManapiUtils.hpp"
#include "./base_http.hpp"
#include "../worker/HTTPv2.hpp"

namespace manapi::net::http {
    struct http_v2_t {
        std::unique_ptr<request_data_t> req;
    };

    enum http_v2_errs {
        EHTTP_V2_PROTOCOL_OK = 0,
        EHTTP_V2_PROTOCOL_ERROR = -1
    };

    int http_v2_work (http_v2_t *ctx, net::site *site);
}

#endif //MANAPIHTTP_HTTP_HTTPV2_HPP
