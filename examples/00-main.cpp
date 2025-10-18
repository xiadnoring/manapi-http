#include <iostream>
#include <format>

#include <manapihttp/ManapiHttp.hpp>
#include <manapihttp/fs/ManapiFilesystem.hpp>
#include <manapihttp/ManapiInitTools.hpp>

int main () {
    manapi::init_tools::log_trace_init (manapi::debug::LOG_TRACE_LOW);
    manapi::async::context::threadpoolfs(4);

    manapi::async::context::gbs (manapi::async::context::blockedsignals());

    auto ctx = manapi::async::context::create(0).unwrap();

    auto router_ctx = manapi::net::http::server_ctx::create().unwrap();

    std::atomic<bool> flag = false;

    ctx->run(ctx, 0, [router_ctx, &flag] (std::function<void()> bind) mutable -> void {
        using http = manapi::net::http::server;
        auto router = http::create(router_ctx).unwrap();

        router.GET ("/", [](http::req &req, manapi::net::http::uresponse resp) -> void {
            resp->text("hello world").unwrap();
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
            if (req.param("key").unwrap() != "123")
            {
                throw std::runtime_error ("bad key");
            }

            flag.exchange(!flag);

            co_return resp.text ("ok").unwrap();
        }).unwrap();

        manapi::async::run([router] () mutable
            -> manapi::future<> {
            manapi::unwrap(co_await router.config("config.json"));
            manapi::unwrap(co_await router.start());
        });

        bind();
    }).unwrap();

    return 0;
}