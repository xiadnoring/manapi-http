#include <iostream>
#include <csignal>
#include <fstream>
#include <zlib.h>
#include <coroutine>
#include <thread>

#include "ManapiHttp.hpp"
#include "ManapiFilesystem.hpp"
#include "include/services/ManapiTaskFunction.hpp"
#include "include/services/ManapiFetch.hpp"
#include "ManapiJsonBuilder.hpp"
#include "ManapiJsonMask.hpp"
#include "ManapiUnicode.hpp"
#include "ManapiHttpMime.hpp"
#include "compress/ManapiHPack.hpp"
#include "ManapiAsync.hpp"
#include "async/ManapiAsyncMutex.hpp"
using namespace manapi::net::utils;
using namespace manapi::net;

using namespace std;

// atomic<int> a = 0;
//
// manapi::net::future<int> print () {
//     a.fetch_add(1);
//     co_return a;
// }
//
// manapi::net::future<void> co_main () {
//     for (int i = 0; i < 10000000; i++) {
//         auto j = co_await print();
//         if (j % 10000 == 0) {
//             cout << j << "\n";
//         }
//     }
// }
//
// int main () {
//     threadpool<task> taskpool (20);
//     taskpool.start();
//     timerpool timerpool (taskpool, 50);
//     taskpool.append_task([&] () -> void { timerpool.start(); });
//
//     auto rhs = co_main ();
//     rhs.get<>(taskpool);
//
//     timerpool.stop();
//     taskpool.stop();
// }

//
int main (int argc, char *argv[]) {
    debug_print_memory("start");
    {
        http::server server;

        server.set_config("./config.json");

        server.GET ("/", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_compress_enabled(true);
            //resp.set_header(HTTP_HEADER.ALT_SVC, stringify_header_value({{"", {{"h3", "\":8888\""}}}}));
            resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
            co_return;
        });

        server.GET ("/response", [&server] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            co_await server.delay(std::chrono::seconds(2));
            printf("2 sec later...\n");
            co_await server.delay(std::chrono::seconds(3));
            printf("3 sec later...\n");
            resp.text("5 sec later...");
            co_return;
        });

        server.GET ("/lenar", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_compress_enabled(false);
            resp.file ("/opt/clion.zip");
            co_return;
        });

        server.GET ("+layer", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_header("test", "test");
            co_return;
        });

        server.OPTIONS("+error", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_header("allow", "OPTIONS, GET, POST");
            resp.set_status(204, HTTP_STATUS.NO_CONTENT_204);
            co_return;
        });

        server.GET ("/test2", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            fetch response ("https://localhost:8888/form");
            response.set_method("POST");
            response.set_body(json2form({
                {"first-name", "Timur"},
                {"last-name", "Zajnullin"},
                {"file", "lol OK ??&?45=ersdf--  \\"}
            }));
            response.enable_ssl_verify(false);
            response.set_custom_setup([] (CURL *curl) -> void {
                curl_easy_setopt(curl, CURLOPT_HTTP_VERSION,
                     (long)CURL_HTTP_VERSION_3);
            });
            resp.text(response.text());
            co_return;
        });

        server.GET ("/video", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_compress_enabled(false);
            resp.set_partial_status(true);
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);

            resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
            co_return;
        });

        // server.GET ("/bigfile", [] (REQ(req), RESP(resp)) {
        //     resp.set_compress_enabled(false);
        //     resp.set_partial_status(false);
        //
        //     resp.file("/home/Timur/Downloads/Фотосессия Иглино.zip");
        // });

        server.GET("/proxy", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            manapi::json jp = {
                    {"error", false},
                    {"message", "this is a list"},
                    {"hello", nullptr}
            };

            try {
                jp["hello"] = req.get_query_param("hello");
            }
            catch (const manapi::net::utils::exception &e)
            {
                std::cerr << e.what() << "\n";
            }

            resp.set_compress_enabled(false);
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JS + ";charset=UTF-8");

            resp.json (jp, 4);
            co_return;
        });

        server.GET ("/text", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            MANAPIHTTP_LOG("{}", "REQ GET");
            resp.set_compress_enabled(false);
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.TEXT_PLAIN);

            std::ifstream f ("/home/Timur/.p10k.zsh");

            if (!f.is_open()) {
                resp.json({
                    {"error", "could not open the file"}
                });

                co_return;
            }

            std::string content;
            while (f) {
                std::string line;
                std::getline(f, line);
                content += line + "\n";
            }

            resp.text (content);
            co_return;
        });

        server.GET ("/+error", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_replacers({
                   {"status_code", std::to_string(resp.get_status_code())},
                   {"status_message", resp.get_status_message()},
                   {"url", "/noooo"}
            });

            resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
            co_return;
        });

        server.POST ("/+error", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON);
            resp.json({
                              {"error", true},
                              {"message", "just a error"}
            });
            co_return;
        });

        server.GET ("/noooo", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);
            resp.set_partial_status(true);
            resp.set_compress_enabled(false);

            resp.file("/home/Timur/Downloads/VideoDownloader/no.mp4");
            co_return;
        });

        server.GET ("/nonoo", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_partial_status(true);
            resp.set_compress_enabled(false);
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);

            resp.file("/home/Timur/Downloads/VideoDownloader/nono.mp4");
            co_return;
        });

        // server.GET ("/[filename]-[extension]", [](REQ(req), RESP(resp)) {
        //     auto &filename     = req.get_param("filename");
        //     auto &extension    = req.get_param("extension");
        //
        //     resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test/" + filename + '.' + extension);
        // });
        //
        // server.GET ("/[filename]-[extension]/+error", [](REQ(req), RESP(resp)) {
        //     resp.set_replacers({
        //            {"status_code", std::to_string(resp.get_status_code())},
        //            {"status_message", resp.get_status_message()},
        //            {"url", "/nonoo"}
        //    });
        //
        //     resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
        // });
        server.GET ("/bigfile", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            std::cout << req.dump() << '\n';
            resp.set_partial_status(false);
            resp.set_compress_enabled(false);

            resp.file("/home/Timur/a.out");
            co_return;
        });

        server.GET ("/мем4", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_partial_status(true);
            resp.file ("/home/Timur/Downloads/video3.mp4");
            co_return;
        });

        server.GET("мемs", "/home/Timur/Downloads/VideoDownloader");

        server.GET ("/favicon.ico", [](REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.file("/home/Timur/Documents/leon.ico");
            co_return;
        });

        server.GET("/test", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            fetch fetch ("http://127.0.0.1:8887/response");
            fetch.set_method("GET");
            resp.text(fetch.text());
            co_return;
        });


        const manapi::json_mask form_mask = {
            {"first-name", "{string(>=5 <50)}"},
            {"last-name", "{string(>=5 <70)}"}
        };

        server.GET("/stop", [&server] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            server.stop();

            resp.text("OK");
            co_return;
        });

        server.GET("/audio", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_partial_status(true);
            resp.set_compress_enabled(true);
            resp.set_compress("gzip");
            resp.file("/home/Timur/Music/Death By Glamour.mp3");
            co_return;
        });

        server.GET("/freeze", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.text(std::to_string(1));
            co_return;
        });

        server.GET ("/largeheader", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            resp.set_header("large", random_string(40000));
            resp.text("hehehehe");
            co_return;
        });

        server.POST ("/form", [] (REQ(req), RESP(resp)) -> manapi::net::future<void> {
            auto formData = co_await req.form();

            resp.set_compress_enabled(false);

            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON + ";charset=UTF-8");

            manapi::json obj = manapi::json::object();

            do {
                if (formData.next_param()) {
                    auto data = co_await formData.get_param();
                    obj.insert(data.first, data.second);
                    continue;
                }
                if (formData.next_file()) {
                    auto data = formData.about_file();
                    obj.insert(data.param_name, std::format("[binary({})]", data.file_name));
                    co_await formData.get_file([] (const char *buff, const size_t &size) -> void {});
                    continue;
                }
                break;
            } while (true);
            cout << obj.dump(2) << "\n";
            resp.json (obj, 4);
            co_return;
        });

        server.GET ("/music", [](REQ(req), RESP(resp)) -> manapi::net::future<void> {
            std::string response;
            for (const auto &file: std::filesystem::directory_iterator ("/home/Timur/Music")) {
                response += std::format("<a href=\"/music/{}\">{}</a><br />", escape_string(file.path().filename()), file.path().filename().string());
            }
            resp.text(response);
            co_return;
        });

        server.GET("/music", "/home/Timur/Music");

        server.GET ("/", "/home/Timur/Desktop/WorkSpace/oneworld/");

        debug_print_memory("pool");

        auto rhs = server.pool(20);
        rhs.get();
        debug_print_memory("preend");
        this_thread::sleep_for(std::chrono::seconds(2));
        // server.stop();
        // this_thread::sleep_for(std::chrono::seconds(100));


    }

    debug_print_memory("end");





    return 0;
}

