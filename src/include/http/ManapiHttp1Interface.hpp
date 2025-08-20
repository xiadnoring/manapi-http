#pragma once

#include "ManapiHttp1.hpp"
#include "worker/ManapiBaseWorker.hpp"
#include "worker/ManapiInterfaceWorker.hpp"

namespace manapi::net::worker {
    struct wrk_http1_ctx_global_t {

    };

    struct wrk_http1_ctx_t {
        wrk_http1_ctx_global_t *gctx;
        char flgs;
        http::request_data_t req;
        std::unique_ptr<manapi::net::http::http_v1_1_t> ctx;
        std::unique_ptr<manapi::net::http::http_v1_1_chunked_t> chunked_ctx;
    };

    struct wrk_http_ctx_global_t {
        std::unique_ptr<wrk_interface_global_t> http1;
        std::unique_ptr<wrk_interface_global_t> http2;
        std::unique_ptr<wrk_interface_global_t> http3;
    };

    manapi::error::status default_wrk_http_all_global_init (wrk_interface_global_t *global, worker::interface_worker *w) MANAPIHTTP_NOEXCEPT;

    manapi::error::status default_wrk_http_all_global_add_version (wrk_interface_global_t *global, int version, std::unique_ptr<wrk_interface_global_t> http_t) MANAPIHTTP_NOEXCEPT;

    manapi::error::status default_wrk_http1_global_init (manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::interface_worker *w) MANAPIHTTP_NOEXCEPT;
}
