#include <iostream>
#include <csignal>
#include <fstream>
#include <zlib.h>

#include "ManapiHttp.hpp"
#include "ManapiFilesystem.hpp"
#include "ManapiTaskFunction.hpp"
#include "ManapiFetch.hpp"
#include "ManapiJsonBuilder.hpp"
#include "ManapiJsonMask.hpp"
#include "ManapiUnicode.hpp"
#include "ManapiHttpMime.hpp"
#include "compress/ManapiHPack.hpp"

using namespace manapi::net::utils;
using namespace manapi::net;

using namespace std;

int main2 () {
    for ( int i  = 0; i < 1000; i++){
        manapi::json b (R"({"hello": "world", "world": "hello", "test": [1, 2, 5.5234, 8e10, 3, 4, 5, true]})", true);
        b.erase("hello");
    }
    manapi::json a = manapi::json::array({78, 78});
    a.push_back(78);
    a[0] += 22;
    cout <<  a.dump(2) << "\n";
    return 0;
}

int main (int argc, char *argv[]) {
    debug_print_memory("start");
    {
        http::server server;

        server.set_config("/home/Timur/Desktop/WorkSpace/ManapiHTTP/cmake-build-debug/config.json");

        server.GET ("/", [] (REQ(req), RESP(resp)) {
            resp.set_compress_enabled(true);
            //resp.set_header(HTTP_HEADER.ALT_SVC, stringify_header_value({{"", {{"h3", "\":8888\""}}}}));
            resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
        });

        server.GET ("/lenar", [] (REQ(req), RESP(resp)) {
            resp.set_compress_enabled(false);
            resp.file ("/opt/clion.zip");
        });

        server.GET ("+layer", [] (REQ(req), RESP(resp)) -> void {
            resp.set_header("test", "test");
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
        });

        server.GET ("/text", [] (REQ(req), RESP(resp)) {
            MANAPIHTTP_LOG("{}", "REQ GET");
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
            resp.set_partial_status(false);
            resp.set_compress_enabled(false);

            resp.file("/home/Timur/a.out");
        });

        server.GET ("/мем4", [] (REQ(req), RESP(resp)) -> void {
            resp.set_partial_status(true);
            resp.file ("/home/Timur/Downloads/video3.mp4");
        });

        server.GET("мемs", "/home/Timur/Downloads/VideoDownloader");

        server.GET ("/favicon.ico", [](REQ(req), RESP(resp)) {
            resp.file("/home/Timur/Documents/leon.ico");
        });

        const manapi::json_mask post_mask = {
            {"first-name", "{string(>=5 <50)}"},
            {"last-name", "{string(>=5 <70)}"},
            {"file", "{any}"}
        };

        server.POST ("/test", [] (REQ(req), RESP(resp)) -> void {
            auto jp = req.json();

            resp.json(jp);
        }, nullptr, post_mask);


        const manapi::json_mask form_mask = {
            {"first-name", "{string(>=5 <50)}"},
            {"last-name", "{string(>=5 <70)}"}
        };

        server.GET("/stop", [&server] (REQ(req), RESP(resp)) -> void {
            server.stop();

            resp.text("OK");
        });

        server.GET("/audio", [] (REQ(req), RESP(resp)) -> void {
            resp.set_partial_status(true);
            resp.set_compress_enabled(true);
            resp.set_compress("gzip");
            resp.file("/home/Timur/Music/Death By Glamour.mp3");
        });

        server.GET("/freeze", [] (REQ(req), RESP(resp)) -> void {
            this_thread::sleep_for(std::chrono::seconds(10));

            resp.text("ok");
        });

        server.GET ("/largeheader", [] (REQ(req), RESP(resp)) -> void {
            resp.set_header("large", random_string(40000));
            resp.text("hehehehe");
        });

        server.POST ("/form", [] (REQ(req), RESP(resp)) {
            auto formData = req.form();

            resp.set_compress_enabled(false);

            resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON + ";charset=UTF-8");

            manapi::json obj = manapi::json::object();

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

                //req.set_file_to_local("/home/Timur/Videos/astral.mp4");

                obj.insert(param_name, {
                    {"name", file_name},
                    {"size", recv}
                });
            }

            std::cout << obj.dump(2) << "\n";

            resp.json (obj, 4);
        });

        server.GET ("/music", [](REQ(req), RESP(resp)) -> void {
            std::string response;
            for (const auto &file: std::filesystem::directory_iterator ("/home/Timur/Music")) {
                response += std::format("<a href=\"/music/{}\">{}</a><br />", escape_string(file.path().filename()), file.path().filename().string());
            }
            resp.text(response);
        });

        server.GET("/music", "/home/Timur/Music");

        server.GET ("/", "/home/Timur/Desktop/WorkSpace/oneworld/");

        debug_print_memory("pool");

        server.pool(20).get();
        // this_thread::sleep_for(std::chrono::seconds(20));
        // server.stop();
        // debug_print_memory("end");
        // this_thread::sleep_for(std::chrono::seconds(100));

    }





    return 0;
}


// int main () {
//     debug_print_memory("start");
//     auto begin = std::chrono::steady_clock::now();
//
//
//     {
//         manapi::json_mask mask2 ({
//             {"hello", "{string(5)[]}"}
//         });
//         manapi::json_mask mask ({
//                 {"hello", manapi::json_mask::ARRAY(manapi::json_mask::OR (manapi::json::array({
//                         {
//                             {"type", "{number(1)}"},
//                             {"data", "{string(<=50)}"},
//                             {"atest", "{string(>=5)|none}"}
//                         },
//                         {
//                             {"type", "{number(2)}"},
//                             {"data", "{number(>=0)}"}
//                         },
//                         {
//                             {"type", "{number(3)}"},
//                             {"file", {
//                                 {"name", "{string(<=50)}"},
//                                 {"size", "{number(>=1000 <=5000)}"}
//                             }}
//                         }
//                     })))
//                 }
//             });
//         // manapi::json a = {
//         //     {"hello", {
//         //         {"type", 3},
//         //         {"file", {
//         //             {"name", "hello"},
//         //             {"size", 78},
//         //             {"hello", "78"}
//         //         }}
//         //     }}
//         // };
//         // std::cout << mask.valid(a) << "\n";
//         manapi::json_builder builder (mask);
//         builder << R"({"hello": [{"type": 1, "data": "78"}, {"type": 2, "data": 78}, {"type": 3, "file": {"name": "file.txt", "size": 1788}}]})";
//         //std::cout << builder.get().dump(2) << "\n";
//
//         manapi::json_mask maskarr = {"{number(>=0)[][]}"};
//         manapi::json_builder builder2 (maskarr);
//         builder2 << R"([[5, 5], [6, 6, 7, 0]])";
//         //cout << builder2.get().dump(2) << "\n";
//         // cout << manapi::json ({"{string(<=150)}", "{number(>=0)[<=1000]}", "{number(>=0)}", "{number(>=0)}", "{string(<=2500)}"}).dump(2) << "\n";
//         manapi::json_mask aa = {
//             {"id", "{number(>=0)}"},
//             {"zone", "{string(<=100)}"},
//             {"do", manapi::json_mask::ARRAY(
//                 manapi::json_mask::OR (manapi::json::array({
//                     {
//                         {"type", "{string(\"force-set\")}"},
//                         {"graph", manapi::json_mask::ARRAY(
//                             manapi::json::array({"{string(<=150)}", "{number(>=0)[<=1000]}", "{number(>=0)}", "{number(>=0)}", "{number()}", "{number()}", "{string(<=2500)}"})
//                         )}
//                     }}
//                 ))
//             )}
//         };
//         cout << aa.get_api_tree().dump(2) << "\n";
//         manapi::json ab (R"({"zone":"PANDA","id":21,"do":[{"type":"force-set","graph":[["",[1],0,0,357,139,"{}"],["",[1],0,1,428,412,"{}"]]}]})", true);
//         cout << aa.valid(ab) << "\n";
//     }
//
//
//     auto end = std::chrono::steady_clock::now();
//     auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
//     std::cout << "The time: " << elapsed_ms.count() << " ms\n";
//     debug_print_memory("start");
// }


