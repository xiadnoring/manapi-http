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
        int next;
        worker::base::connection_io_part top;
    };

    enum http_v1_1_errs {
        EHTTP_V1_1_PROTOCOL_OK = 0,
        EHTTP_V1_1_PROTOCOL_ERROR = -1,
        EHTTP_V1_1_PROTOCOL_UPGRADE = -2,
        EHTTP_V1_1_PROTOCOL_WANT_READ = -3
    };

    enum http_v1_1_chunked_errs {
        EHTTP_V1_1_CHUNKED_OK = 0,
        EHTTP_V1_1_CHUNKED_ERR = -1,
        EHTTP_V1_1_CHUNKED_READ = -2,
        EHTTP_V1_1_CHUNKED_WAIT = -3
    };

    bool http_v1_1_is_token_char (const char &c);
    int http_v1_1_work (http_v1_1_t *ctx, http::config *config, const char **nbuffer, ssize_t *nsize);
    int http_v1_1_chunked_read (http_v1_1_chunked_t *ctx, worker::base *worker, const worker::shared_conn &conn, http::config *config, const char *buffer, ssize_t size);
    int http_v1_1_chunked_flush (http_v1_1_chunked_t *ctx, worker::base *worker, const worker::shared_conn &conn);
}
