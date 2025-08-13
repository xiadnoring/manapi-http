#include <iostream>
#include <format>

#include <manapihttp/ManapiHttp.hpp>
#include <manapihttp/ManapiFilesystem.hpp>
#include <manapihttp/ManapiInitTools.hpp>

int main () {
    manapi::init_tools::log_trace_init (manapi::debug::LOG_TRACE_LOW);
    manapi::async::context::threadpoolfs(4);

    auto ctx = manapi::async::context::create(0).unwrap();
    ctx->eventloop()->setup_handle_interrupt();

    auto router_ctx = manapi::async::context::create().unwrap();

    std::atomic<bool> flag = false;

    manapi::async::context::run(ctx, [router_ctx, &flag] () mutable -> void {
        using http = manapi::net::http::server;
        auto router = http::create(router_ctx).unwrap();

        router.GET ("/", [](http::req &req, manapi::net::http::response *resp) -> void {
            resp->text("hello world").unwrap();
            resp->finish();
        }).unwrap();

        router.GET ("/", ".").unwrap();

        router.GET ("/+error", [](manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
            resp.replacers ({
                {"status_code", std::to_string(resp.status_code())},
                {"status_message", std::string{resp.status_message()}}
            });
            co_return resp.file ("error.html").unwrap();
        }).unwrap();

        router.POST ("/api/+error", [](manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
            co_return resp.json ({
               {"error", true},
               {"message", "An error has occurred"}
            }).unwrap();
        }).unwrap();

        router.GET ("/api/[key]/toggle", [&flag](manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
            if (req.param("key") != "123")
            {
                throw std::runtime_error ("bad key");
            }

            flag.exchange(!flag);

            co_return resp.text ("ok").unwrap();
        }).unwrap();

        manapi::async::run(ctx, [router] () mutable
            -> manapi::future<> {
            (co_await router.config("config.json")).unwrap();
            (co_await router.start()).unwrap();
        });
    }).unwrap();

    return 0;
}
