#include <iostream>
#include <format>
#include <manapihttp/ManapiHttp.hpp>
#include <manapihttp/ManapiTaskHttp.hpp>
#include <manapihttp/ManapiFilesystem.hpp>

using namespace manapi::net::utils;
using namespace manapi::net;
using namespace std;

int main ()
{
    std::atomic<bool> flag = false;

    http server;

    server.set_config ("config.json");

    server.GET ("/", [](REQ(req), RESP(resp)) {
        resp.text("hello world");
    });

    server.GET ("/", ".");

    server.GET ("/+error", [](REQ(req), RESP(resp)) {
        std::map <std::string, std::string> replacers = {
            {"status_code", std::to_string(resp.get_status_code())},
            {"status_message", resp.get_status_message()}
        };
        resp.set_replacers (replacers);
        resp.file ("error.html");
    });

    server.POST ("/api/+error", [](REQ(req), RESP(resp)) {
        resp.json ({
           {"error", true},
           {"message", "An error has occurred"}
       });
    });

    server.POST ("/api/[key]/form", [&flag](REQ(req), RESP(resp)) {
        if (req.get_param("key") != "123")
        {
            throw std::runtime_error ("bad key");
        }

        json jp = {
            {"flag", flag ? "yes" : "no"}
        };

        auto formData = req.form();

        for (auto &item: formData)
        {
            jp.insert(item.first, item.second);
        }

        resp.json(jp, 4);
    });

    server.GET ("/api/[key]/toggle", [&flag](REQ(req), RESP(resp)) {
        if (req.get_param("key") != "123")
        {
            throw std::runtime_error ("bad key");
        }

        flag = !flag;

        resp.text ("ok");
    });

    std::cout << "Hello world\n";

    server.pool(20).get();

    return 0;
}
