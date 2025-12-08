#include <iostream>

#include <ManapiHttp.hpp>
#include <ManapiInitTools.hpp>

#define FOLDER "/home/Timur/Downloads/anime-main/"
int main() {
    manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_HARD);

    manapi::async::context::threadpoolfs(4);
    manapi::async::context::gbs(manapi::async::context::blockedsignals());

    auto ctx = manapi::async::context::create(0).unwrap();

    auto server_ctx = manapi::net::http::server_ctx::create().unwrap();
    ctx->run(0, [server_ctx] (auto cb) -> void {
        using http = manapi::net::http::server;
        auto route = manapi::net::http::server::create(server_ctx).unwrap();

        route.GET ("/", [] (http::req &req, http::uresp resp) -> void {
            resp->file (manapi::filesystem::path::join(FOLDER, "index.html")).unwrap();
        }).unwrap();

        route.GET ("/", FOLDER, [] (http::req &req, http::uresp resp) -> void {
            resp->compress_enabled(true);
        }).unwrap();

        manapi::async::run ([route] () mutable -> manapi::future<> {
            manapi::unwrap(co_await route.config(manapi::filesystem::path::join(".", "config.json")));
            manapi::unwrap(co_await route.start ());
        });

        cb();
    }).unwrap();

    manapi::clear_tools::curl_library_clear();
    manapi::clear_tools::ev_library_clear();
    manapi::clear_tools::ssl_library_clear();

    return 0;
}