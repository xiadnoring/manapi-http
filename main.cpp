#include <iostream>
#include <csignal>
#include <fstream>

#include "ManapiHttp.h"
#include "ManapiTaskHttp.h"
#include "ManapiFilesystem.h"
#include "ManapiTaskFunction.h"
#include "ManapiFetch.h"
#include "ManapiJsonMask.h"

using namespace manapi::utils;
using namespace manapi::net;
using namespace std;

// TODO: json custom exceptions
// TODO: json bug replace a = a + b (somewhere)

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
        json jp = {
                {"error", false},
                {"message", "this is a list"},
                {"hello", nullptr}
        };

        try {
            jp["hello"] = req.get_query_param("hello");
        }
        catch (const manapi::utils::exception &e)
        {
            std::cerr << e.what() << "\n";
        }

        resp.set_compress_enabled(false);
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
        resp.set_header(http_header.CONTENT_TYPE, http_mime.APPLICATION_JSON);
        resp.json({
                          {"error", true},
                          {"message", "just a error"}
        });
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

    const json_mask post_mask = {
        {"first-name", "{string(>=5 <50)}"},
        {"last-name", "{string(>=5 <70)}"},
        {"file", "{any}"}
    };

    server.POST ("/test", [] (REQ(req), RESP(resp)) -> void {
        auto jp = req.json();

        resp.json(jp);
    }, nullptr, post_mask);

    {
        const json_mask form_mask = {
            {"first-name", "{string(>=5 <50)}"},
            {"last-name", "{string(>=5 <70)}"}
        };

        server.POST ("/form", [] (REQ(req), RESP(resp)) {
            auto formData = req.form();

            resp.set_compress_enabled(false);

            resp.set_header(http_header.CONTENT_TYPE, http_mime.APPLICATION_JSON + ";charset=UTF-8");

            json obj = json::object();

            for (const auto &item: formData)
            {
                obj.insert(item.first, item.second);
            }

    //        while (req.has_file())
    //        {
    //            obj.insert(req.inf_file().param_name, req.inf_file().file_name);
    //
    //            std::ofstream f (manapi::filesystem::join ("..", "text"), std::ios::binary);
    //
    //            req.set_file([&f] (const char *buff, size_t size) {
    //                f.write (buff, (ssize_t) size);
    //            });
    //
    //            f.close();
    //        }

            resp.json (obj, 4);
        }, nullptr, form_mask);
    }

    server.GET ("/", "/home/Timur/Desktop/WorkSpace/oneworld/");

    server.pool();

    return 0;
}
