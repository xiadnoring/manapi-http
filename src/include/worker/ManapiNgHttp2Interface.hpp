#pragma once

#include <memory>
#include "worker/ManapiHttp2Worker.hpp"

namespace manapi::net::worker {
    struct wrk_nghttp2_ctx_global_t {
        std::shared_ptr<net::worker::http_v2> worker;
    };

    struct wrk_nghttp2_ctx_t {
        wrk_nghttp2_ctx_global_t *gctx;
        char flgs;
        //std::unique_ptr<manapi::net::http::http_v2_t> ctx;
    };

    manapi::error::status default_wrk_nghttp2_global_init (manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w);
}