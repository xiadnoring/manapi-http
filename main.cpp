#include <iostream>
#include <csignal>
#include <fstream>

#include "ManapiHttp.hpp"
#include "ManapiTaskHttp.hpp"
#include "ManapiFilesystem.hpp"
#include "ManapiTaskFunction.hpp"
#include "ManapiFetch.hpp"
#include "ManapiJsonMask.hpp"

using namespace manapi::utils;
using namespace manapi::net;
using namespace std;

int main(int argc, char *argv[]) {
    debug_print_memory("start");
    {
        http server;

        server.set_config("/home/Timur/Desktop/WorkSpace/ManapiHTTP/cmake-build-debug/config.json");

        server.GET ("/", [] (REQ(req), RESP(resp)) {
            resp.set_compress_enabled(true);
            resp.set_header(HTTP_HEADER.ALT_SVC, stringify_header_value({{"", {{"h3", "\":8888\""}}}}));
            resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
        });

        server.GET ("/lenar", [] (REQ(req), RESP(resp)) {
            resp.set_compress_enabled(false);
            resp.file ("/opt/clion.zip");
        });

        server.OPTIONS("+error", [] (REQ(req), RESP(resp)) -> void {
            resp.set_header("allow", "OPTIONS, GET, POST");
            resp.set_status(204, HTTP_STATUS.NO_CONTENT_204);
        });

        server.GET ("/test2", [] (REQ(req), RESP(resp)) -> void {
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
        });

        server.GET ("/video", [] (REQ(req), RESP(resp)) {
            resp.set_compress_enabled(false);
            resp.set_partial_status(true);
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);

            resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
        });

        // server.GET ("/bigfile", [] (REQ(req), RESP(resp)) {
        //     resp.set_compress_enabled(false);
        //     resp.set_partial_status(false);
        //
        //     resp.file("/home/Timur/Downloads/Фотосессия Иглино.zip");
        // });

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
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JS + ";charset=UTF-8");

            resp.json (jp, 4);
        });

        server.GET ("/text", [] (REQ(req), RESP(resp)) {
            MANAPI_LOG("{}", "REQ GET");
            resp.set_compress_enabled(false);
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.TEXT_PLAIN);

            std::ifstream f ("/home/Timur/.p10k.zsh");

            if (!f.is_open()) {
                resp.json({
                    {"error", "could not open the file"}
                });

                return;
            }

            std::string content;
            while (f) {
                std::string line;
                std::getline(f, line);
                content += line + "\n";
            }

            resp.text (content);
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
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON);
            resp.json({
                              {"error", true},
                              {"message", "just a error"}
            });
        });

        server.GET ("/noooo", [] (REQ(req), RESP(resp)) {
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);
            resp.set_partial_status(true);
            resp.set_compress_enabled(false);

            resp.file("/home/Timur/Downloads/VideoDownloader/no.mp4");
        });

        server.GET ("/nonoo", [] (REQ(req), RESP(resp)) {
            resp.set_partial_status(true);
            resp.set_compress_enabled(false);
            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);

            resp.file("/home/Timur/Downloads/VideoDownloader/nono.mp4");
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
        server.GET ("/bigfile", [] (REQ(req), RESP(resp)) {
            std::cout << req.dump() << '\n';
            resp.set_partial_status(true);
            resp.set_compress_enabled(false);

            resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO");
        });

        server.GET ("/favicon.ico", [](REQ(req), RESP(resp)) {
            resp.file("/home/Timur/Documents/leon.ico");
        });

        const json_mask post_mask = {
            {"first-name", "{string(>=5 <50)|none}"},
            {"last-name", "{string(>=5 <70)}"},
            {"file", "{any}"}
        };

        server.POST ("/test", [] (REQ(req), RESP(resp)) -> void {
            auto jp = req.json();

            resp.json(jp);
        }, nullptr, post_mask);


        const json_mask form_mask = {
            {"first-name", "{string(>=5 <50)}"},
            {"last-name", "{string(>=5 <70)}"}
        };

        server.GET("/stop", [&server] (REQ(req), RESP(resp)) -> void {
            server.stop();

            resp.text("OK");
        });

        server.GET("/audio", [] (REQ(req), RESP(resp)) -> void {
            resp.set_partial_status(true);
            resp.set_compress_enabled(false);
            resp.file("/home/Timur/Music/Death By Glamour.mp3");
        });

        server.GET("/freeze", [] (REQ(req), RESP(resp)) -> void {
            this_thread::sleep_for(std::chrono::seconds(10));

            resp.text("ok");
        });

        server.POST ("/form", [] (REQ(req), RESP(resp)) {
            auto formData = req.form();

            resp.set_compress_enabled(false);

            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON + ";charset=UTF-8");

            json obj = json::object();

            for (const auto &item: formData)
            {
                obj.insert(item.first, item.second);
            }

            while (req.has_file())
            {
                ssize_t recv = 0;

                std::string param_name = req.inf_file().param_name;
                std::string file_name = req.inf_file().file_name;

                req.set_file([&recv] (const char *buff, const size_t &size) {
                    recv += size;
                    if (size > 4096 * 3) {
                        printf("WHAT\n");
                    }
                });

                obj.insert(param_name, {
                    {"name", file_name},
                    {"size", recv}
                });
            }

            std::cout << obj.dump(2) << "\n";

            resp.json (obj, 4);
        });

        server.GET ("/", "/home/Timur/Desktop/WorkSpace/oneworld/");

        debug_print_memory("pool");

        server.pool(20).get();
    }

    debug_print_memory("end");

    MANAPI_LOG("wait {}s", 5);
    sleep(5);

    debug_print_memory("end");

    return 0;
}
