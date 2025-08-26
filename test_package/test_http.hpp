#pragma once

#ifdef MANAPIHTTP_HTTP_AS_EXECUTABLE
#   include "ManapiHttp.hpp"
#else
#   include <manapihttp/ManapiHttp.hpp>
#endif
#include "./utest.h"

UTEST(http, http_router_exists) {
    using http = manapi::net::http::server;

    auto server = manapi::net::http::server_ctx::create();
    auto router = manapi::net::http::server::create(server.unwrap()).unwrap();

    router.GET ("/hello", [] (http::req &req, http::uresp resp) -> void {
        resp.finish();
    }).unwrap();

    auto res = router.GET ("/hello", [] (http::req &req, http::uresp resp) -> void {
        resp.finish();
    });

    ASSERT_TRUE_MSG(!res.ok(), "already exists");
}