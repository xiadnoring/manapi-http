#include <iostream>
#include <format>

#include <manapihttp/ManapiHttp.hpp>
#include <manapihttp/ManapiFilesystem.hpp>

int main ()
{
    auto ctx = manapi::async::context::create(4,0.02);
    manapi::net::http::server router (ctx);

    std::atomic<bool> flag = false;

    router.config ("config.json");

    router.GET ("/", [](manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
        co_return resp.text("hello world");
    });

    router.GET ("/", ".");

    router.GET ("/+error", [](manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
        resp.replacers ({
            {"status_code", std::to_string(resp.status_code())},
            {"status_message", std::string{resp.status_message()}}
        });
        co_return resp.file ("error.html");
    });

    router.POST ("/api/+error", [](manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
        co_return resp.json ({
           {"error", true},
           {"message", "An error has occurred"}
        });
    });

    router.POST ("/api/[key]/form", [&flag](manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
        if (req.param("key") != "123")
        {
            throw std::runtime_error ("bad key");
        }

        manapi::json data = {
            {"flag", flag ? "yes" : "no"}
        };

        auto formData = co_await req.form();
        while (true) {
            if (formData.next_file()) {
                co_await formData.save_file("/dev/null");
            }
            else if (formData.next_param()) {
                auto item = co_await formData.get_param();
                data.insert(item.first, item.second);
            }
            else {
                break;
            }
        }

        co_return resp.json(data, 4);
    });

    router.GET ("/api/[key]/toggle", [&flag](manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
        if (req.param("key") != "123")
        {
            throw std::runtime_error ("bad key");
        }

        flag.exchange(!flag);

        co_return resp.text ("ok");
    });

    manapi::async::run(ctx, [router] () mutable
        -> manapi::future<> { return router.start(); });

    ctx->sync_start();

    return 0;
}
