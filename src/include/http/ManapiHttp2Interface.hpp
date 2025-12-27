#pragma once

#include "ManapiHttp2.hpp"
#include "worker/ManapiBaseWorker.hpp"
#include "worker/ManapiHttp2Worker.hpp"

namespace manapi::net::worker {
    struct wrk_http2_ctx_global_t {
        std::shared_ptr<net::worker::http_v2> worker;
    };

    struct wrk_http2_ctx_t {
        wrk_http2_ctx_global_t *gctx;
        char flgs;
        std::unique_ptr<manapi::net::http::http_v2_t> ctx;
    };

    manapi::status default_wrk_http2_global_init (manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::interface_worker *w);
}
