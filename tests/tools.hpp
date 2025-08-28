#pragma once

#include "ManapiInitTools.hpp"
#include "ManapiProcess.hpp"
#include "ManapiString.hpp"
#include "ManapiHttp.hpp"
#include "json/ManapiJson.hpp"
#include "std/ManapiEasyCancellation.hpp"

#define HTTP1PORT "8888"
#define MANAPIHTTP_TESTS_MAIN UTEST_STATE(); \
int main(int argc, const char *const argv[]) { \
    try { manapi::init_tools::log_trace_init((manapi::debug::trace_level)std::stoi(manapi::process::get_env("MANAPIHTTP_LOGTRACE").unwrap())); }\
    catch (...) { manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_NONE); }\
    manapi::async::context::threadpoolfs(2); \
    manapi::async::context::gbs = manapi::async::context::blockedsignals(); \
    return utest_main(argc, argv); \
}

inline manapi::async::shared_ctx init_ctx (std::size_t timout_in_ms = 8000) {
    auto ctx = manapi::async::context::create(4).unwrap();
    ctx->eventloop()->setup_handle_interrupt();
    /* task killer */
    ctx->timerpool()->append_timer_sync(timout_in_ms, [timout_in_ms] (manapi::timer t) -> void {
        manapi_log_error("timeout in %zu ms was reached", timout_in_ms);
        exit(-1);
    }).unwrap();
    return ctx;
}

inline void wait_ctx (manapi::async::shared_ctx ctx) {
    manapi::async::context::run(ctx, 0, [] (std::function<void()> bind) -> void {

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
                {"max_buffer_stack", 1},
                {"max_merge_buffer_stack", 1},
                {"max_connections", 2},
                {"max_connections_by_ip", 2},
                {"keep_alive", 0}
            });
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