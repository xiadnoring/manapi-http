#pragma once

#include "../ManapiUtils.hpp"
#include "./base_http.hpp"
#include "../ManapiHttpConfig.hpp"
#include "components/ManapiURLDecodeStream.hpp"

namespace manapi::net::http {
    struct http_v1_1_t {
        int current;
        int next;
        int http;
        std::string s1;
        std::string s2;
        std::unique_ptr<request_data_t> req;
    };

    struct http_v1_1_chunked_t {
        int left;
        int state;
        std::string prev;
    };

    enum http_v1_1_errs {
        EHTTP_V1_1_PROTOCOL_OK = 0,
        EHTTP_V1_1_PROTOCOL_ERROR = -1,
        EHTTP_V1_1_PROTOCOL_UPGRADE = -2,
        EHTTP_V1_1_PROTOCOL_WANT_READ = -3
    };

    int http_v1_1_work (http_v1_1_t *ctx, net::site *site, const char **nbuffer, ssize_t *nsize);
    manapi::future<ssize_t> http_v1_1_chunked_read (http_v1_1_chunked_t *ctx, worker::base *worker, worker::connection *conn, net::site *site, char *buffer, ssize_t size);
}
