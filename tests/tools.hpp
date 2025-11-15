#pragma once

#ifdef MANAPIHTTP_BUILD_SHARED_LIBS
#   undef MANAPIHTTP_BUILD_SHARED_LIBS
#endif

#include "ManapiInitTools.hpp"
#include "ManapiProcess.hpp"
#include "ManapiString.hpp"
#include "ManapiHttp.hpp"
#include "json/ManapiJson.hpp"
#include "ManapiTimerPool.hpp"
#include "std/ManapiEasyCancellation.hpp"

#define HTTP1PORT "8888"
#define HTTP2PORT "8887"
#define MANAPIHTTP_TESTS_MAIN UTEST_STATE(); \
int main(int argc, const char *const argv[]) { \
    try { manapi::init_tools::log_trace_init((manapi::debug::trace_level)std::stoi(manapi::process::get_env("MANAPIHTTP_LOGTRACE").unwrap())); }\
    catch (...) { manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_NONE); }\
    manapi::async::context::threadpoolfs(2); \
    manapi::async::context::gbs (manapi::async::context::blockedsignals()); \
    return utest_main(argc, argv); \
}

inline manapi::async::shared_ctx init_ctx (int *utest_result, std::size_t timout_in_ms = 60000) {
    auto ctx = manapi::async::context::create(0).unwrap();
    /* task killer */
    ctx->timerpool()->append_interval_sync(timout_in_ms, manapi::TIMER_DEFAULT,[utest_result, timout_in_ms, flg = bool(false)] (manapi::timer t) mutable -> void {
        manapi_log_error("timeout in %zu ms was reached", timout_in_ms);
        *utest_result = UTEST_TEST_FAILURE;
        if (flg)
            exit(-1);
        flg = true;
        manapi::async::run(manapi::async::current()->stop());
    }).unwrap();
    return ctx;
}

inline void wait_ctx (manapi::async::shared_ctx ctx) {
    ctx->run([] (std::function<void()> bind) -> void {

        bind();
    });
}


inline manapi::net::http::server init_router (manapi::json cnf, std::move_only_function<manapi::future<>()> cb) {
    using http = manapi::net::http::server;

    auto server = manapi::net::http::server_ctx::create();
    auto router = manapi::net::http::server::create(server.unwrap()).unwrap();

    router.GET ("/", [] (http::req &req, http::uresp resp) -> void {
        resp->text("Hello, World!");
    }).unwrap();

    manapi::async::run ([router, cnf, cb = std::move(cb)] () mutable -> manapi::future<> {
        std::string certkey;
        std::string certpem;

        std::string current = manapi::filesystem::path::current_path();
        while (true) {
            auto st = co_await manapi::filesystem::async_exists(manapi::filesystem::path::join(current, "examples", "self-signed-ssl"));
            if (st.ok()) {
                if (st.unwrap())
                    break;
            }
            else {
                st.err().log();
            }
            current = manapi::filesystem::path::join(current, "..");
            if (!current.contains(manapi::filesystem::path::delimiter)) {
                manapi_log_error("!!! failed to find self-signed-ssl !!!");
                co_await manapi::async::current()->stop();
                break;
            }
        }
        current = manapi::filesystem::path::join(current, "examples", "self-signed-ssl");

        manapi::json config = {
            {"pools", manapi::json::array()}
        };
        if (cnf.contains("http1") && cnf["http1"].as_bool_cast()) {
            config["pools"].push_back({
                {"address", "127.0.0.1"},
                {"port", HTTP1PORT},
                {"http", manapi::json::array("1.1")},
                {"transport", "tcp"},
                {"tcp_no_delay", true},
                {"simultaneous_accepts", true},
                {"buffer_size", 4096},
                {"ssl", {
                    {"verify_peer", false},
                    {"key", manapi::filesystem::path::join(current, "cert.key")},
                    {"cert", manapi::filesystem::path::join(current, "cert.crt")},
                    {"ticket", false},
                    {"enable", true}
                }},
                {"max_buffer_stack", 2},
                {"max_merge_buffer_stack", 1},
                {"max_connections", 2},
                {"max_connections_by_ip", 2},
                {"keep_alive", 0}
            });

            if (cnf.contains("http1_cnf") && cnf["http1_cnf"].is_object()) {
                auto &custom = cnf["http1_cnf"];
                for (auto & it : custom.entries())
                    config["pools"].as_array().back()[it.first] =  std::move(it.second);
            }
        }
        if (cnf.contains("http2") && cnf["http2"].as_bool_cast()) {
            config["pools"].push_back({
                {"address", "127.0.0.1"},
                {"port", HTTP2PORT},
                {"http", manapi::json::array("2")},
                {"transport", "tls"},
                {"ssl", {
                    {"verify_peer", false},
                    {"key", manapi::filesystem::path::join(current, "cert.key")},
                    {"cert", manapi::filesystem::path::join(current, "cert.crt")},
                    {"ticket", false},
                    {"enable", true}
                }},
                {"max_concurrent_streams", 6},
                {"tcp_no_delay", true},
                {"simultaneous_accepts", true},
                {"buffer_size", 4096},
                {"max_buffer_stack", 2},
                {"max_merge_buffer_stack", 1},
                {"max_connections", 100},
                {"max_connections_by_ip", 100},
                {"keep_alive", 0}
            });

            if (cnf.contains("http2_cnf") && cnf["http2_cnf"].is_object()) {
                auto &custom = cnf["http2_cnf"];
                for (auto & it : custom.entries())
                    config["pools"].as_array().back()[it.first] =  std::move(it.second);
            }
        }
        auto res = co_await router.config_object (config);
        res.unwrap();

        res = co_await router.start();
        res.unwrap();

        co_await cb();

        co_await manapi::async::current()->stop();
    });

    return router;
}