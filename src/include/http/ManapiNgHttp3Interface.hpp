#pragma once

#include <memory>

#include "ManapiUtils.hpp"
#include "worker/ManapiHttp3Worker.hpp"
#include "../ManapiUtils.hpp"

#if defined( MANAPIHTTP_NGHTTP3_DEPENDENCY ) && MANAPIHTTP_NGHTTP3_DEPENDENCY

namespace manapi::net::worker {
    struct ng_wrk_http3_ctx_global_t;

    struct ng_wrk_http3_ctx_t;

    manapi::error::status ng_wrk_http3_global_init (manapi::net::worker::wrk_interface_global_t *global, manapi::net::worker::interface_worker *w) MANAPIHTTP_NOEXCEPT;
}

#endif