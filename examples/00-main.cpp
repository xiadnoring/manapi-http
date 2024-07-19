#include <iostream>
#include <format>
#include <manapihttp/ManapiHttp.h>
#include <manapihttp/ManapiTaskHttp.h>
#include <manapihttp/ManapiFilesystem.h>

using namespace manapi::utils;
using namespace manapi::net;
using namespace std;

int main ()
{
    std::atomic<bool> flag = false;

    http server;

    server.set_config ("config.json");

    server.GET ("/", "/static/files");

    server.GET ("/+error", [](REQ(req), RESP(resp)) {
        resp.text (std::format("ERROR: {} {}", resp.get_status_code(), resp.get_status_message()));
    });

    server.POST ("/api/+error", [](REQ(req), RESP(resp)) {
        resp.text (R"({ "error": true, "message": "An error has occurred"})");
    });

    server.POST ("/api/[key]/form", [&flag](REQ(req), RESP(resp)) {
        if (*req.get_param("key") != "123")
        {
            throw std::runtime_error ("bad key");
        }

        json jp = json::object();
        jp.insert ("flag", flag ? "yes" : "no");

        auto formData = req.form();

        for (auto &item: formData)
        {
            jp.insert(item.first, item.second);
        }

        resp.text (jp.dump(4));
    });

    server.GET ("/api/[key]/toggle", [&flag](REQ(req), RESP(resp)) {
        if (req.get_param("key") != "123")
        {
            throw std::runtime_error ("bad key");
        }

        flag = !flag;

        resp.text ("ok");
    });

    auto f = server.run();

    f.get();

    return 0;
}
