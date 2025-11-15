#include "ManapiInitTools.hpp"
#include "ManapiProcess.hpp"
#include "ManapiFetch2.hpp"
#include "ManapiString.hpp"
#include "ManapiHttp.hpp"
#include "json/ManapiJson.hpp"
#include "std/ManapiEasyCancellation.hpp"

#include "./utest.h"
#include "./tools.hpp"


#ifdef MANAPIHTTP_FETCH_SUPPORT

UTEST (http_and_fetch, keep_alive_1) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx(utest_result, 20000);
    auto router = init_router({
        {"http1", true},
        {"http1_cnf", {
            {"keep_alive", 5},
            {"speed_check_delay", 1},
            {"speed_check_bytes", 100000000}
        }}
    }, [&] () -> manapi::future<> {

        for (int i =0 ; i < 2; i++) {
            if (i) {
                co_await manapi::async::delay{1500};
            }
            manapi::json jparams = {
                {"method", "GET"}
            };

            auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/keep_alive", std::move(jparams));

            auto fetch = fetch_res.unwrap();
#define return co_return
            ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

            auto text_res = co_await fetch.text();
            auto text = text_res.unwrap();

#define return co_return
            ASSERT_TRUE_MSG((text == "OK"), "check response message");
#undef return
        }

    });

    router.GET("keep_alive", [] (http::req &req, http::uresp resp) -> void {
        resp->text("OK").unwrap();
    }).unwrap();

    wait_ctx(ctx);
}
/**
#   ifdef MANAPIHTTP_OPENSSL_DEPENDENCY

UTEST (http_and_fetch, tls_keep_alive_1) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx(utest_result, 60000);
    auto router = init_router({
        {"http1", true},
        {"http1_cnf", {
            {"keep_alive", 1},
            {"speed_check_delay", 1},
            {"speed_check_bytes", 100000000},
            {"transport", "tls"},
            {"implementation", "openssl"}
        }}
    }, [&] () -> manapi::future<> {

        for (int i =0 ; i < 2; i++) {
            if (i) {
                co_await manapi::async::delay{1500};
            }
            manapi::json jparams = {
                {"method", "GET"}
            };

            auto fetch_res = co_await manapi::net::fetch2::fetch("https://127.0.0.1:" HTTP1PORT "/keep_alive", std::move(jparams));

            auto fetch = fetch_res.unwrap();
#define return co_return
            ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

            auto text_res = co_await fetch.text();
            auto text = text_res.unwrap();

#define return co_return
            ASSERT_TRUE_MSG((text == "OK"), "check response message");
#undef return
        }

    });

    router.GET("keep_alive", [] (http::req &req, http::uresp resp) -> void {
        resp->text("OK").unwrap();
    }).unwrap();

    wait_ctx(ctx);
}

UTEST (http_and_fetch, tls_http2_1) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx(utest_result, 60000);
    auto router = init_router({
        {"http2", true},
        {"http2_cnf", {
            {"keep_alive", 1},
            {"speed_check_delay", 1},
            {"speed_stream_check_delay", 1},
            {"speed_stream_check_bytes", 100000000},
            {"speed_check_bytes", 100000000},
            {"transport", "tls"},
            {"implementation", "openssl"}
        }}
    }, [&] () -> manapi::future<> {

        for (int i =0 ; i < 2; i++) {
            if (i) {
                co_await manapi::async::delay{1500};
            }
            manapi::json jparams = {
                {"method", "GET"}
            };

            auto fetch_res = co_await manapi::net::fetch2::fetch("https://127.0.0.1:" HTTP2PORT "/keep_alive", std::move(jparams));

            auto fetch = fetch_res.unwrap();
#define return co_return
            ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

            auto text_res = co_await fetch.text();
            auto text = text_res.unwrap();

#define return co_return
            ASSERT_TRUE_MSG((text == "OK"), "check response message");
#undef return
        }

    });

    router.GET("keep_alive", [] (http::req &req, http::uresp resp) -> void {
        resp->text("OK").unwrap();
    }).unwrap();

    wait_ctx(ctx);
}

#   endif**/

#endif

MANAPIHTTP_TESTS_MAIN