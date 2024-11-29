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
    std::vector <uint8_t> a1 = {130, 65, 138, 160, 228, 29, 19, 157, 9, 184, 243, 207, 61, 135, 4, 136, 98, 83, 217, 74, 243, 166, 154, 63, 88, 135, 164, 126, 86, 28, 197, 128, 31, 64, 135, 65, 72, 177, 39, 90, 209, 255, 183, 254, 113, 28, 243, 80, 85, 47, 79, 97, 233, 47, 243, 247, 222, 15, 228, 34, 127, 249, 250, 83, 249, 210, 116, 193, 15, 249, 118, 193, 213, 39, 243, 247, 222, 15, 229, 239, 231, 233, 79, 230, 244, 246, 30, 147, 91, 79, 243, 247, 222, 15, 228, 34, 127, 249, 64, 139, 65, 72, 177, 39, 90, 209, 173, 73, 227, 53, 5, 2, 63, 48, 64, 141, 65, 72, 177, 39, 90, 209, 173, 93, 3, 76, 167, 178, 159, 7, 34, 76, 105, 110, 117, 120, 34, 64, 146, 182, 185, 172, 28, 133, 88, 213, 32, 164, 182, 194, 173, 97, 123, 90, 84, 37, 31, 1, 49, 122, 206, 208, 127, 102, 162, 129, 176, 218, 224, 83, 250, 252, 8, 126, 212, 206, 106, 173, 242, 167, 151, 156, 137, 198, 191, 181, 33, 174, 186, 11, 200, 177, 230, 50, 88, 109, 151, 87, 101, 197, 63, 172, 216, 247, 232, 207, 244, 165, 6, 234, 85, 49, 20, 157, 79, 253, 169, 122, 123, 15, 73, 88, 8, 159, 92, 11, 129, 112, 41, 184, 114, 142, 195, 48, 219, 46, 174, 203, 159, 83, 229, 73, 124, 165, 137, 211, 77, 31, 67, 174, 186, 12, 65, 164, 199, 169, 143, 51, 166, 154, 63, 223, 154, 104, 250, 29, 117, 208, 98, 13, 38, 61, 76, 121, 166, 143, 190, 208, 1, 119, 254, 141, 72, 230, 43, 3, 238, 105, 126, 141, 72, 230, 43, 30, 11, 29, 127, 70, 164, 115, 21, 129, 215, 84, 223, 95, 44, 124, 253, 246, 128, 11, 189, 244, 58, 235, 160, 196, 26, 76, 122, 152, 65, 166, 168, 178, 44, 95, 36, 156, 117, 76, 95, 190, 240, 70, 207, 223, 104, 0, 187, 191, 64, 138, 65, 72, 180, 165, 73, 39, 89, 6, 73, 127, 136, 64, 233, 42, 199, 176, 211, 26, 175, 64, 138, 65, 72, 180, 165, 73, 39, 90, 147, 200, 95, 134, 168, 125, 205, 48, 210, 95, 64, 138, 65, 72, 180, 165, 73, 39, 90, 212, 22, 207, 2, 63, 49, 64, 138, 65, 72, 180, 165, 73, 39, 90, 66, 161, 63, 134, 144, 228, 182, 146, 212, 159, 115, 145, 157, 41, 173, 23, 24, 98, 131, 144, 116, 78, 116, 38, 227, 207, 60, 243, 31, 80, 146, 155, 217, 171, 250, 82, 66, 203, 64, 210, 95, 165, 35, 179, 233, 79, 104, 76, 159, 81, 156, 178, 213, 182, 240, 250, 178, 223, 190, 208, 1, 119, 254, 139, 82, 220, 55, 125, 246, 128, 11, 189, 244, 90, 190, 251, 64, 5, 221, 96, 148, 139, 213, 49, 90, 105, 32, 11, 162, 11, 47, 1, 160, 132, 32, 11, 78, 190, 227, 204, 255, 96, 140, 139, 213, 49, 73, 0, 93, 16, 89, 120, 13, 4, 63, 96, 150, 138, 97, 193, 138, 16, 174, 21, 194, 219, 205, 58, 211, 109, 56, 21, 194, 232, 130, 203, 192, 104, 34, 96, 166, 138, 97, 196, 214, 12, 46, 61, 126, 247, 28, 58, 224, 197, 184, 43, 133, 112, 186, 32, 178, 240, 26, 8, 87, 10, 225, 92, 46, 136, 44, 188, 6, 155, 109, 112, 46, 5, 193, 64, 134, 174, 195, 30, 195, 39, 215, 133, 182, 0, 125, 40, 111,};
    std::vector <uint8_t> a2 = {130, 209, 135, 4, 137, 98, 81, 247, 49, 15, 82, 230, 33, 255, 205, 203, 207, 206, 83, 177, 53, 35, 152, 172, 15, 185, 165, 250, 53, 35, 152, 172, 120, 44, 117, 253, 26, 145, 204, 86, 7, 93, 83, 125, 26, 145, 204, 86, 17, 222, 111, 247, 230, 154, 62, 141, 72, 230, 43, 31, 63, 95, 44, 124, 253, 246, 128, 11, 189, 202, 127, 10, 133, 168, 235, 16, 246, 35, 127, 9, 132, 53, 35, 152, 191, 115, 151, 157, 41, 173, 23, 24, 98, 131, 144, 116, 78, 116, 38, 227, 207, 60, 243, 18, 158, 202, 87, 157, 52, 209, 200, 199, 198, 197, 196, 195, 127, 3, 133, 182, 0, 253, 40, 111,  };
    compress::hpack::decoder_t dec (65535);
    dec.decode(string (a1.begin(), a1.end()));
    for (const auto &header: dec.headers()) {
        cout << header.first << " " << header.second << "\n";
    }
    std::cout << "\n";
    std::cout << "\n";
    dec.decode(string (a2.begin(), a2.end()));
    auto headers = dec.headers();
    for (const auto &header: headers) {
        cout << header.first << " " << header.second << "\n";
    }
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


