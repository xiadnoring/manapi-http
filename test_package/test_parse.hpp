#pragma once

#ifdef MANAPIHTTP_HTTP_AS_EXECUTABLE
#    include "ManapiHttp.hpp"
#    include "ManapiString.hpp"

#    include "http/ManapiHttp1.hpp" // http_v1_1_t
#else
#    include <manapihttp/ManapiHttp.hpp>
#    include <manapihttp/ManapiString.hpp>

#    include <manapihttp/http/ManapiHttp1.hpp> // http_v1_1_t
#endif
#include "./utest.h"

UTEST(parse, headers_ok_1) {
    auto v = manapi::net::http::parse_header(R"(hello: world1;"test"=56,"world2";test="78")");
    ASSERT_TRUE(v.ok());
    auto h = v.value();
    auto hv = manapi::net::http::parse_header_value(h.second);
    auto hk = std::string{h.first};
    manapi::string::lower_ascii(hk);

    ASSERT_TRUE(hk == "hello");
    ASSERT_TRUE(hv[0].value == "world1");
    ASSERT_TRUE(hv[0].params["test"] == "56");
    ASSERT_TRUE(hv[1].value == "world2");
    ASSERT_TRUE(hv[1].params["test"] == "78");
}

UTEST(parse, http_v1_1_ok_1) {
    char buffer1[] = "GET / HTTP/1.1\r\nUser-Agent: test/1.2.3\r\nAccept: */*\r\n";
    char buffer2[] = "\r\n";

    manapi::net::http::config config {manapi::json::object()};
    manapi::net::http::http_v1_1_t ctx{};

    const char *next = buffer1;
    ssize_t size = sizeof (buffer1) - 1;

    auto rhs = manapi::net::http::http_v1_1_work(&ctx, &config, &next, &size);
    ASSERT_EQ(rhs, manapi::net::http::EHTTP_V1_1_PROTOCOL_WANT_READ);


    ASSERT_EQ(size, 0);
    ASSERT_EQ((std::size_t)(buffer1 + sizeof (buffer1) - 1), (std::size_t)next);

    next = buffer2;
    size = sizeof (buffer2) - 1;

    rhs = manapi::net::http::http_v1_1_work(&ctx, &config, &next, &size);
    ASSERT_EQ(rhs, manapi::net::http::EHTTP_V1_1_PROTOCOL_OK);

    ASSERT_EQ(size, 0);
    ASSERT_EQ((std::size_t)(buffer2 + sizeof (buffer2) - 1), (std::size_t)next);
}

UTEST(parse, http_v1_1_ok_2) {
    char buffer1[] = "PRI * HTTP/2.0\r\n\r\n";

    manapi::net::http::config config {manapi::json::object()};
    manapi::net::http::http_v1_1_t ctx{};

    const char *next = buffer1;
    ssize_t size = sizeof (buffer1) - 1;

    auto rhs = manapi::net::http::http_v1_1_work(&ctx, &config, &next, &size);
    ASSERT_EQ(rhs, (int)manapi::net::http::EHTTP_V1_1_PROTOCOL_UPGRADE);

    ASSERT_EQ(size, 4);
    ASSERT_EQ((std::size_t)(buffer1 + sizeof (buffer1) - 1), (std::size_t)next + 4);
}

UTEST(parse, http_v1_1_ok_3) {
    char buffer1[] = "GET /page?hello=world HTTP/1.1\r\nHeader1: Key1\r\nHeader2:  Key2 \r\n\r\n";

    manapi::net::http::config config {manapi::json::object()};
    manapi::net::http::http_v1_1_t ctx{};

    const char *next = buffer1;
    ssize_t size = sizeof (buffer1) - 1;

    auto rhs = manapi::net::http::http_v1_1_work(&ctx, &config, &next, &size);
    ASSERT_EQ(rhs, (int)manapi::net::http::EHTTP_V1_1_PROTOCOL_OK);

    ASSERT_EQ(size, 0);
    ASSERT_EQ((std::size_t)(buffer1 + sizeof (buffer1) - 1), (std::size_t)next);

    auto req = std::move(ctx.req);

    ASSERT_EQ(req->divided, 1);
    ASSERT_TRUE(req->uri == "/page?hello=world");
    ASSERT_TRUE(req->path[0] == "page");
    ASSERT_TRUE(req->path[1] == "?hello=world");

    ASSERT_TRUE(req->headers["header1"] == "Key1");
    ASSERT_TRUE(req->headers["header2"] == " Key2");
}