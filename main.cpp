// #include <iostream>
// #include <memory>
//
// #include <ManapiHttp.hpp>
// #include <memory>
// #include <async/ManapiAsyncContext.hpp>
// #include <extensions/pq/AsyncPostgreClient.hpp>
//
// #include "services/ManapiFetch.hpp"
//
// int main () {
//     curl_global_init(CURL_GLOBAL_DEFAULT);
//
//     auto ctx = manapi::async::context::create();
//     auto db = std::make_shared<manapi::ext::pq::connection>(ctx);
//     auto router = std::make_shared<manapi::net::http::server> (ctx);
//
//     ctx->eventloop()->setup_handle_interrupt();
//
//     router->set_config("./config.json");
//
//     router->GET ("/", [cnt = std::make_shared<std::atomic<int>>(0)] (decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) mutable -> manapi::future<> {
//         co_return resp.text(std::format("Hello World! Count: {}", cnt->fetch_add(1)));
//     });
//
//     router->GET("/+error", [](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
//         resp.set_replacers({
//             {"status_code", std::to_string(resp.get_status_code())},
//             {"status_message", resp.get_status_message()}
//         });
//
//         co_return resp.file ("../examples/error.html");
//     });
//
//     router->POST("/+error", [](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
//         co_return resp.json({{"error", resp.get_status_code()},
//                 {"msg", resp.get_status_message()}});
//     });
//
//     router->GET("/cat", [&ctx](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
//         manapi::net::fetch fetch (ctx, "https://localhost:8887/");
//         fetch.enable_ssl_verify(false);
//         fetch.set_method("GET");
//         //fetch.set_verbose(true);
//         auto data = co_await fetch.text();
//
//         co_return resp.text(std::move(data));
//     });
//
//     router->GET("/pq", [db, mx = std::make_shared<manapi::async::mutex>(ctx)](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
//         auto lk = co_await mx->lock_guard();
//         /* The pool of database connections here / This example is so slow */
//         auto res = co_await db->exec("SELECT * FROM for_test WHERE id > $1", 0);
//
//         lk.call();
//
//         std::string content;
//         for (const auto &row: res) {
//             content += row["id"].as<int>() + " - " + row["str_col"].as<std::string>() + "<hr/>";
//         }
//
//         co_return resp.text(std::move(content));
//     });
//
//     router->GET("/proxy", [](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
//         co_return resp.proxy("http://127.0.0.1:8889/video");
//     });
//
//     router->GET("/video", [](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
//         resp.set_partial_status(true);
//         resp.set_compress_enabled(false);
//         co_return resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
//     });
//
//     router->GET("/stop", [ctx](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
//         /* stop the app */
//         co_await ctx->stop();
//         co_return resp.text("stopped");
//     });
//
//     router->GET("/timeout", [ctx](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
//         /* stop the app */
//         co_await manapi::async::delay{ctx, std::chrono::seconds(10)};
//         co_return resp.text("10sec");
//     });
//
//     manapi::async::run(ctx, [router, db] () -> manapi::future<> {
//         co_await db->connect("127.0.0.1", "7879", "development", "rv8FY--PHz_QV<wvT4=n_Ru+cUJE}>KCqmBj9&#M3\\\"Gb.tx", "workflow-main");
//         co_await router->start();
//     });
//
//     ctx->sync_start();
//
//     return 0;
// }






#include <iostream>
#include <csignal>
#include <fstream>
#include <zlib.h>
#include <coroutine>
#include <fcntl.h>
#include <features.h>
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
#include "ManapiString.hpp"
#include "async/ManapiAsyncContext.hpp"
#include "async/ManapiAsyncConditionVariable.hpp"
#include "async/ManapiAsyncContext.hpp"
#include "async/ManapiAsyncMutex.hpp"
#include "components/ManapiChain.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "crypto/ManapiAES.hpp"
#include "extensions/pq/AsyncPostgreClient.hpp"
#include "extensions/pq/AsyncPostgreValue.hpp"
#include "services/ManapiEventLoop.hpp"
#include "worker/tools/OpenSSLTools.hpp"

using namespace manapi::net;

using namespace std;


int main (int argc, char *argv[]) {
    worker::tools::ssl_library_init();
    manapi::debug::debug_print_memory("start");
    std::string data = "hello world test 2";
    {
        auto ctx = manapi::async::context::create(1, 0.001);
        ctx->eventloop()->setup_handle_interrupt();

        // manapi::async::run(ctx, [ctx] () -> manapi::future<> {
        //             auto data = co_await manapi::filesystem::read_async(ctx, "/home/Timur/Desktop/WorkSpace/test/msg.txt");
        //             std::cout << "recv: " << data << "\n";
        //         });
       // manapi::async::run(ctx, manapi::filesystem::write_async(ctx, "/home/Timur/Desktop/WorkSpace/test/msg.txt", "hello world! Helicopter 32", 0777));

        {
            http::server server (ctx);
            server.set_config("./config.json");

            server.GET ("/", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.set_compress_enabled(true);
                resp.set_header(HTTP_HEADER.ALT_SVC, http::stringify_header_value({{"", {{"h3", "\":8888\""}, {"ma", "86400"}}}}));
                resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
                co_return;
            });

            server.GET ("/response", [&server] (REQ(req), RESP(resp)) -> manapi::future<void> {
                printf("3 sec later...\n");
                co_await manapi::async::delay{server.async_context(), 5000};
                resp.text("5 sec later...");
                co_return;
            });

            server.GET ("/lenar", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.set_compress_enabled(false);
                resp.file ("/opt/clion.zip");
                co_return;
            });

            server.GET ("+layer", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.set_header("test", "test");
                co_return;
            });

            server.OPTIONS("+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.set_header("allow", "OPTIONS, GET, POST");
                resp.set_status(204, HTTP_STATUS.NO_CONTENT_204);
                co_return;
            });

            server.GET ("/test2", [ctx] (REQ(req), RESP(resp)) -> manapi::future<void> {
                fetch response (ctx, "http://localhost:8889");
                response.set_method("GET");
                // response.set_body(json2form({
                //     {"first-name", "Timur"},
                //     {"last-name", "Zajnullin"},
                //     {"file", "lol OK ??&?45=ersdf--  \\"}
                // }));
                response.enable_ssl_verify(false);
                // response.set_custom_setup([] (CURL *curl) -> void {
                //     curl_easy_setopt(curl, CURLOPT_HTTP_VERSION,
                //          (long)CURL_HTTP_VERSION_3);
                // });
                resp.text(co_await response.text());
                co_return;
            });

            server.GET ("/video", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
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

            server.GET("/proxy", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                manapi::json jp = {
                        {"error", false},
                        {"message", "this is a list"},
                        {"hello", nullptr}
                };

                try {
                    jp["hello"] = req.get_query_param("hello");
                }
                catch (const manapi::exception &e)
                {
                    std::cerr << e.what() << "\n";
                }

                resp.set_compress_enabled(false);
                resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JS + ";charset=UTF-8");

                resp.json (jp, 4);
                co_return;
            });

            server.GET ("/text", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
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

            server.GET ("/+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.set_replacers({
                       {"status_code", std::to_string(resp.get_status_code())},
                       {"status_message", resp.get_status_message()},
                       {"url", "/noooo"}
                });

                resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
                co_return;
            });

            server.POST ("/+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON);
                resp.json({
                                  {"error", true},
                                  {"message", "just a error"}
                });
                co_return;
            });

            server.GET ("/noooo", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);
                resp.set_partial_status(true);
                resp.set_compress_enabled(false);

                resp.file("/home/Timur/Downloads/VideoDownloader/no.mp4");
                co_return;
            });

            server.GET ("/nonoo", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
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
            server.GET ("/bigfile", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                std::cout << req.dump() << '\n';
                resp.set_partial_status(false);
                resp.set_compress_enabled(false);

                resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO");
                co_return;
            });

            server.GET ("/мем4", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.set_partial_status(true);
                resp.file ("/home/Timur/Downloads/video3.mp4");
                co_return;
            });

            server.GET("мемs", "/home/Timur/Downloads/VideoDownloader");

            server.GET ("/favicon.ico", [](REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.file("/home/Timur/Documents/leon.ico");
                co_return;
            });

            server.GET("/test", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                // fetch fetch ("http://127.0.0.1:8887/response");
                // fetch.set_method("GET");1
                // resp.text(fetch.text());
                    std::cout << req.get_header("test").size() << "\n";
                co_return;
            });


            const manapi::json_mask form_mask = {
                {"first-name", "{string(>=5 <50)}"},
                {"last-name", "{string(>=5 <70)}"}
            };

            server.GET("/stop", [&server] (REQ(req), RESP(resp)) -> manapi::future<void> {
                co_await server.stop();

                resp.text("OK");
                co_return;
            });

            server.GET("/audio", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.set_partial_status(true);
                resp.set_compress_enabled(true);
                resp.set_compress("gzip");
                resp.file("/home/Timur/Music/Death By Glamour.mp3");
                co_return;
            });

            server.GET("/freeze", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.text(std::to_string(1));
                co_return;
            });

            server.GET ("/largeheader", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
                resp.set_header("set-cookie", manapi::string::random(8000));
                resp.text("hehehehe");
                co_return;
            });

            server.POST ("/form", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
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

            server.GET ("/music", [ctx](REQ(req), RESP(resp)) -> manapi::future<void> {
                std::string response;
                response += manapi::crypto::strdec2strhex(co_await manapi::crypto::random_string_async(ctx, 100)) + "<hr />";
                for (const auto &file: std::filesystem::directory_iterator ("/home/Timur/Music")) {
                    std::string filename = file.path().filename().string();
                    response += std::format("<a href=\"/music/{}\">{}</a><br />", manapi::unicode::escape_string(filename), file.path().filename().string());
                }
                resp.text(response);
                co_return;
            });

            server.GET("/music", "/home/Timur/Music");

            server.GET ("/", "/home/Timur/Desktop/WorkSpace/oneworld/");

            manapi::debug::debug_print_memory("pool");

            manapi::async::run (ctx, server.start());

            ctx->sync_start();

            manapi::debug::debug_print_memory("preend");
        }
        manapi::debug::debug_print_memory("preend 2");

        auto &b = manapi::async::async_tasks;
        printf("ASYNC STACK: %zi\n", b.size());

        manapi::async::async_tasks = {};
        manapi::debug::debug_print_memory("preend 8");
    }

    manapi::debug::debug_print_memory("end");

    return 0;
}
