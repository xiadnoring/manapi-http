#include <iostream>
#include <csignal>
#include "ManapiHttp.h"
#include "ManapiTaskHttp.h"
#include "ManapiFilesystem.h"


using namespace manapi::toolbox;
using namespace manapi::net;
using namespace std;

static http server;

void handler_interrupt (int sig_int) {
    server.stop();
}


int main(int argc, char *argv[]) {
    signal (SIGABRT, handler_interrupt);
    signal (SIGKILL, handler_interrupt);
    signal (SIGTERM, handler_interrupt);

    server.set_config("config.json");

    server.GET ("/", [] (REQ(req), RESP(resp)) {
        resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
    });

    server.GET ("/video", [] (REQ(req), RESP(resp)) {
        resp.set_compress_enabled(false);
        resp.set_partial_status(true);
        resp.set_header(http_header.CONTENT_TYPE, "video/mp4");

        resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
    });

    server.GET ("/text", [] (REQ(req), RESP(resp)) {
        resp.set_compress_enabled(false);
        resp.set_header(http_header.CONTENT_TYPE, "text/plain");

        resp.file ("/home/Timur/.p10k.zsh");
    });

    server.GET ("/+error", [] (REQ(req), RESP(resp)) {
        resp.set_replacers({
                                   {"status_code", std::to_string(resp.get_status_code())},
                                   {"status_message", resp.get_status_message()},
                                   {"url", "/noooo"}
                           });

        resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
    });

    server.POST ("/+error", [] (REQ(req), RESP(resp)) {
        resp.text(R"({"errno": 0, "message": "Please, stop!"})");
    });

    server.GET ("/noooo", [] (REQ(req), RESP(resp)) {
        resp.set_header(http_header.CONTENT_TYPE, "video/mp4");
        resp.set_partial_status(true);
        resp.set_compress_enabled(false);

        resp.file("/home/Timur/Downloads/VideoDownloader/no.mp4");
    });

    server.GET ("/nonoo", [] (REQ(req), RESP(resp)) {
        resp.set_partial_status(true);
        resp.set_compress_enabled(false);
        resp.set_header(http_header.CONTENT_TYPE, "video/mp4");

        resp.file("/home/Timur/Downloads/VideoDownloader/nono.mp4");
    });

    server.GET ("/[filename]-[extension]", [](REQ(req), RESP(resp)) {
        auto filename     = req.get_param("filename");
        auto extension    = req.get_param("extension");

        resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test/" + *filename + '.' + *extension);
    });

    server.GET ("/[filename]-[extension]/+error", [](REQ(req), RESP(resp)) {
        resp.set_replacers({
                                   {"status_code", std::to_string(resp.get_status_code())},
                                   {"status_message", resp.get_status_message()},
                                   {"url", "/nonoo"}
                           });

        resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
    });

    server.GET ("/favicon.ico", [](REQ(req), RESP(resp)) {
        resp.file("/home/Timur/Documents/leon.ico");
    });

    server.POST ("/form", [] (REQ(req), RESP(resp)) {
        auto formData = req.form();


        json obj = json::object();

        for (const auto &item: formData)
        {
            obj.insert(item.first, item.second);
        }

        resp.text(obj.dump(4));
    });

    server.GET ("/", "/home/Timur/Desktop/WorkSpace/oneworld/");

    auto f1 = server.run();

    f1.get();

    return 0;
}
