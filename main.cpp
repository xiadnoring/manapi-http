#include <iostream>
#include <csignal>
#include "ManapiHttp.h"
#include "ManapiTaskHttp.h"
#include "ManapiFilesystem.h"

#define TIME

#ifdef TIME
#include <ctime>
#endif

using namespace manapi::toolbox;
using namespace manapi::net;
using namespace std;

static http server;

void handler_interrupt (int sig_int) {
    server.stop();
}


int main(int argc, char *argv[]) {
#ifdef TIME
    clock_t start = clock();
#endif

    signal (SIGABRT, handler_interrupt);
    signal (SIGKILL, handler_interrupt);
    signal (SIGTERM, handler_interrupt);

    server.set_config("config.json");

    server.GET ("/", [] (http_request &req, http_response &resp) {
        //if (req.contains_header(http_header.USER_AGENT))
        //    printf("%s\n", req.get_header(http_header.USER_AGENT).data());
        resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
    });

    server.GET ("/video", [] (http_request &req, http_response &resp) {
        resp.set_compress_enabled(false);
        resp.set_header(http_header.CONTENT_TYPE, "video/mp4");
        resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4", true);
    });

    server.GET ("/text", [] (http_request &req, http_response &resp) {
        resp.set_compress_enabled(false);
        resp.set_header(http_header.CONTENT_TYPE, "text/plain");

        resp.file ("/home/Timur/.p10k.zsh");
    });

    // what the hack is that ?)
    server.GET ("/+error", [] (http_request &req, http_response &resp) {
         resp.text(R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Error</title>

    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.0.2/dist/css/bootstrap.min.css" rel="stylesheet" integrity="sha384-EVSTQN3/azprG1Anm3QDgpJLIm9Nao0Yz1ztcQTwFspd3yD65VohhpuuCOmLASjC" crossorigin="anonymous">
    <style>
        body {
            background-color: black;
        }
        video {
            position: absolute;
            inset: 0;
            margin: auto;
            max-width: 100%;
            max-height: 100%;
            user-select: none;
            -moz-user-focus: normal;
        }
        .m-container {
            width: 100vw;
            height: 100vh;

            display: flex;
            justify-content: center;
            flex-direction: column;
            align-items: center;
        }
        .m-center {
            z-index: 1000;
            text-align: center;
        }
        .m-center > .header{
            font-size: 100px;
        }
        body * {
            color: #ffffff;
            -webkit-text-stroke: 1px #000; /* Толщина и цвет обводки */
              text-stroke: 2px #000;
        }
    </style>
</head>
<body>
    <video id="video" src="/noooo" autoplay></video>

    <div class="m-container">
        <div class="m-center">
            <div class="header">)" + std::to_string(resp.get_status_code()) + R"(</div>
            <h3>)" + resp.get_status_message() + R"(</h3>
        </div>
    </div>

    <script>
        var video = document.getElementById("video");

        video.play();
    </script>
</body>
</html>)");
    });

    server.POST ("/+error", [] (http_request &req, http_response &resp) {
        resp.text(R"({"errno": 0, "message": "Please, stop!"})");
    });

    server.GET ("/noooo", [] (auto &req, http_response &resp) {
        resp.set_header(http_header.CONTENT_TYPE, "video/mp4");
        resp.file("/home/Timur/Downloads/VideoDownloader/no.mp4", true);
    });

    server.GET ("/nonoo", [] (auto &req, auto &resp) {
        printf ("video is downloading\n");
        resp.set_header(http_header.CONTENT_TYPE, "video/mp4");
        resp.file("/home/Timur/Downloads/VideoDownloader/nono.mp4", true);
    });

    server.GET ("/[filename]-[extension]", [](http_request &req, http_response &resp) {
        auto filename     = req.get_param("filename");
        auto extension    = req.get_param("extension");

        //resp.file("/home/Timur/Desktop/WorkSpace/oneworld/hello.html");
        resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test/" + *filename + '.' + *extension);
    });

    server.GET ("/[filename]-[extension]/+error", [](http_request &req, http_response &resp) {
        resp.text(R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Error</title>

    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.0.2/dist/css/bootstrap.min.css" rel="stylesheet" integrity="sha384-EVSTQN3/azprG1Anm3QDgpJLIm9Nao0Yz1ztcQTwFspd3yD65VohhpuuCOmLASjC" crossorigin="anonymous">
    <style>
        body {
            background-color: black;
        }
        video {
            position: absolute;
            inset: 0;
            margin: auto;
            max-width: 100%;
            max-height: 100%;
            user-select: none;
            -moz-user-focus: normal;
        }
        .m-container {
            width: 100vw;
            height: 100vh;

            display: flex;
            justify-content: center;
            flex-direction: column;
            align-items: center;
        }
        .m-center {
            z-index: 1000;
            text-align: center;
        }
        .m-center > .header{
            font-size: 100px;
        }
        body * {
            color: #ffffff;
            -webkit-text-stroke: 1px #000; /* Толщина и цвет обводки */
              text-stroke: 2px #000;
        }
    </style>
</head>
<body>
    <video id="video" src="/nonoo" autoplay></video>

    <div class="m-container">
        <div class="m-center">
            <div class="header">)" + std::to_string(resp.get_status_code()) + R"(</div>
            <h3>)" + resp.get_status_message() + R"(</h3>
        </div>
    </div>

    <script>
        var video = document.getElementById("video");

        video.play();
    </script>
</body>
</html>)");
    });

    server.GET ("/hello", [](http_request &req, http_response &resp) {
        resp.set_compress_enabled(true);

        resp.text("hello there are hello there are hello there are hello there are hello there are hello there are hello there are hello there are hello there are");
        resp.set_status(200, http_status.OK_200);
    });

    server.POST ("/api/hello", [](http_request &req, http_response &resp) {
        auto body = req.form();

        for (auto &c: body) {
            std::cout << c.first << " - " << c.second << "\n";
        }

        json obj (R"({"files": []})");

        obj.insert("msg", "The request has been processed");

        while (req.has_file()) {
            printf("%s\n", req.inf_file().file_name.data());

            json file = json::object();

            file.insert("name", req.inf_file().file_name);
            file.insert("mime", req.inf_file().mime_type);

            obj["files"].push_back(file);

            req.set_file([] (const char *buff, size_t s) {});
        }

        resp.text(obj.dump());
    });

    server.GET ("/favicon.ico", [](http_request &req, http_response &resp) {
        resp.file("/home/Timur/Documents/leon.ico");
    });

    auto f1 = server.run();

    f1.get();

    printf ("OK\n");
#ifdef TIME
    clock_t stop = clock();
    printf("Time spent: %f s\n", (double)(stop - start)/CLOCKS_PER_SEC);
#endif

    return 0;
}
