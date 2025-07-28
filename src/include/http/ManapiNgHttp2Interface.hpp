#pragma once

#include <memory>
#include "worker/ManapiHttp2Worker.hpp"
#include "../ManapiUtils.hpp"

#if MANAPIHTTP_NGHTTP2_DEPENDENCY

namespace manapi::net::worker {
    struct ng_wrk_http2_ctx_global_t;

    struct ng_wrk_http2_ctx_t;

    manapi::error::status ng_wrk_http2_global_init (manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::base *w);
}

#endif