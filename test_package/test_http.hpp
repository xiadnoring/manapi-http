#pragma once

#ifdef MANAPIHTTP_HTTP_AS_EXECUTABLE
#   include "ManapiHttp.hpp"
#else
#   include <manapihttp/ManapiHttp.hpp>
#endif
#include "./utest.h"

UTEST(http, http_router_exists) {
    auto server = manapi::net::http::server_ctx::create();
    auto http = manapi::net::http::server::create(server.unwrap()).unwrap();

    http.GET ("/hello", [] (manapi::net::http::request &req, manapi::net::http::response *resp) -> void {
        resp->finish();
    }).unwrap();

    auto res = http.GET ("/hello", [] (manapi::net::http::request &req, manapi::net::http::response *resp) -> void {
        resp->finish();
    });

    ASSERT_TRUE_MSG(!res.ok(), "already exists");
}