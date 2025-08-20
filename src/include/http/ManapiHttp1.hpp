#pragma once

#include "ManapiUtils.hpp"
#include "http/ManapiBaseHttp.hpp"
#include "http/ManapiHttpConfig.hpp"
#include "http/ManapiURLDecodeStream.hpp"

namespace manapi::net::http {
    struct http_v1_1_t {
        uint32_t size;
        int current;
        int next;
        int http;
        std::string s1;
        std::string s2;
    };

    struct http_v1_1_chunked_t {
        uint32_t left;
        int state;
        int next;
        worker::connection_io_part top;
        std::set<std::string> trailer_names;
        std::string s1;
        std::string s2;
    };

    enum http_v1_1_errs {
        EHTTP_V1_1_PROTOCOL_OK = 0,
        EHTTP_V1_1_PROTOCOL_PAYLOAD_TOO_LARGE,
        EHTTP_V1_1_PROTOCOL_ERROR,
        EHTTP_V1_1_PROTOCOL_UPGRADE,
        EHTTP_V1_1_PROTOCOL_WANT_READ
    };

    enum http_v1_1_chunked_errs {
        EHTTP_V1_1_CHUNKED_OK = 0,
        EHTTP_V1_1_CHUNKED_ERR = -1,
        EHTTP_V1_1_CHUNKED_READ = -2,
        EHTTP_V1_1_CHUNKED_WAIT = -3
    };

    bool http_v1_1_is_token_char (const char &c) MANAPIHTTP_NOEXCEPT;
    int http_v1_1_work (http_v1_1_t *ctx, request_data_t *req, http::config *config, const char **nbuffer, ssize_t *nsize) MANAPIHTTP_NOEXCEPT;
    int http_v1_1_chunked_read (http_v1_1_chunked_t *ctx, std::map<std::string, std::string, std::less<>> *trailers, uint32_t *trailers_size, worker::base *worker, const worker::shared_conn &conn, http::config *config, const char *buffer, ssize_t size) MANAPIHTTP_NOEXCEPT;
    int http_v1_1_chunked_flush (http_v1_1_chunked_t *ctx, worker::base *worker, const worker::shared_conn &conn) MANAPIHTTP_NOEXCEPT;
}
