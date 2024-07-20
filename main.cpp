#include <iostream>
#include <csignal>
#include "ManapiHttp.h"
#include "ManapiTaskHttp.h"
#include "ManapiFilesystem.h"
#include "ManapiTaskFunction.h"
#include "ManapiFetch.h"

using namespace manapi::utils;
using namespace manapi::net;
using namespace std;

int main(int argc, char *argv[]) {
    http server;
    future<int> f;

    server.set_config("config.json");

    server.GET ("/", [] (REQ(req), RESP(resp)) {
        resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
    });

    server.GET ("/video", [] (REQ(req), RESP(resp)) {
        resp.set_compress_enabled(false);
        resp.set_partial_status(true);
        resp.set_header(http_header.CONTENT_TYPE, http_mime.VIDEO_MP4);

        resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
    });

    server.GET("/proxy", [] (REQ(req), RESP(resp)) {
        json jp (R"({"hello\"": "sf\324-0-o`//qwe'lsdfpok"})");

        resp.set_header(http_header.CONTENT_TYPE, http_mime.APPLICATION_JS + ";charset=UTF-8");

        resp.json (jp, 4);
    });

    server.GET ("/text", [] (REQ(req), RESP(resp)) {
        resp.set_compress_enabled(false);
        resp.set_header(http_header.CONTENT_TYPE, http_mime.TEXT_PLAIN);

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
        resp.set_header(http_header.CONTENT_TYPE, http_mime.VIDEO_MP4);
        resp.set_partial_status(true);
        resp.set_compress_enabled(false);

        resp.file("/home/Timur/Downloads/VideoDownloader/no.mp4");
    });

    server.GET ("/nonoo", [] (REQ(req), RESP(resp)) {
        resp.set_partial_status(true);
        resp.set_compress_enabled(false);
        resp.set_header(http_header.CONTENT_TYPE, http_mime.VIDEO_MP4);

        resp.file("/home/Timur/Downloads/VideoDownloader/nono.mp4");
    });

    server.GET ("/[filename]-[extension]", [](REQ(req), RESP(resp)) {
        auto &filename     = req.get_param("filename");
        auto &extension    = req.get_param("extension");

        resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test/" + filename + '.' + extension);
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

        resp.json (obj, 4);
    });

    server.GET ("/", "/home/Timur/Desktop/WorkSpace/oneworld/");

    server.pool();

    return 0;
}
