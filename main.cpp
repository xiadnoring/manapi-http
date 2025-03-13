#include <iostream>
#include <memory>

#include <ManapiHttp.hpp>
#include <memory>
#include <async/ManapiAsyncContext.hpp>
#include <extensions/pq/AsyncPostgreClient.hpp>

#include "components/ObjectPool.hpp"
#include "ManapiFilesystem.hpp"
#include "ManapiHttpMime.hpp"
#include "services/ManapiFetch.hpp"
#include "services/ManapiFetch2.hpp"
#include "ManapiInitTools.hpp"

int main () {
    auto ctx = manapi::async::context::create();
    auto db = std::make_shared<manapi::ext::pq::connection>(ctx);
    auto router = std::make_shared<manapi::net::http::server> (ctx);

    /** 1000ms **/
    db->timeout(1000);

    ctx->eventloop()->setup_handle_interrupt();

    router->set_config("./config.json");

    router->GET ("/", [ctx, cnt = std::make_shared<std::atomic<int>>(0)] (decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) mutable -> manapi::future<> {

        co_return resp.text(std::format("Hello World! Count: {}", cnt->fetch_add(1)));
    });

    router->GET("/murtaza", [ctx, cnt = std::make_shared<std::atomic<int>>(0)] (decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) mutable -> manapi::future<> {
        co_await manapi::async::delay{ctx, 5000};
        co_return resp.text("hello ,murtaza");
    });

    router->GET("/+error", [](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        resp.set_replacers({
            {"status_code", std::to_string(resp.get_status_code())},
            {"status_message", resp.get_status_message()}
        });

        co_return resp.file ("../examples/error.html");
    });

    router->POST("/+error", [](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        co_return resp.json({{"error", resp.get_status_code()},
                {"msg", resp.get_status_message()}});
    });

    router->GET("/cat/[id]", [&ctx](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        manapi::net::fetch fetch (ctx, "https://dragonball-api.com/api/planets/" + req.get_param("id"));
        fetch.enable_ssl_verify(false);
        fetch.set_method("GET");
        //fetch.set_verbose(true);
        auto data = co_await fetch.json();

        co_return resp.text(std::move(data["description"].as_string()));
    });

    router->GET("/pq/[id]", [db, mx = std::make_shared<manapi::async::mutex>(ctx)](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        auto lk = co_await mx->lock_guard();
        /* The pool of database connections here / This example is so slow */
        try {
            auto res = co_await db->exec("INSERT INTO for_test (id, str_col) VALUES ($2, $1);","no way", std::stoll(req.get_param("id")));
        }
        catch (...) {

        }

        auto res = co_await db->exec("SELECT * FROM for_test;");
        lk.call();

        std::string content = "b";
        for (const auto &row: res) {
            content += std::to_string(row["id"].as<int>()) + " - " + row["str_col"].as<std::string>() + "<hr/>";
        }

        co_return resp.text(std::move(content));
    });

    router->POST ("/json", [ctx] (REQ(req), RESP(resp)) -> manapi::future<void> {
         auto res = co_await req.json();
         resp.json(std::move(res));
    }, nullptr, {
           {"email", "{string(>=5 <=50)}"},
           {"password", "{string(>=5 <=50)}"}
       });

    router->GET("/proxy", [ctx](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        try {
            auto response = co_await manapi::net::fetch2::fetch (ctx, "https://localhost:8888/test1", {
                {"enable_alpn", false}, {"enable_ssl_verify", false}, {"enable_http2", true},{"method", "POST"}, {
                    "headers", {
                        {"content-length", 100}
                    }
                }}, [s = ssize_t(100)] (char *buffer, ssize_t size) mutable -> manapi::future<ssize_t> {
                    auto res = std::min(s, size);
                    s -= res;
                    memset(buffer, 'h', res);
                    co_return -1;
                });
            if (!response->ok()) {
                std::cout << "no ok\n";
            }
            co_return resp.json (co_await response->json());
        }
        catch (...) {

        }
        co_return resp.text("BAD");
        // co_return resp.proxy("https://127.0.0.1:8888/video", [] (manapi::net::fetch &proxy) -> void {
        //     proxy.enable_alpn(false);
        //     proxy.enable_http2();
        //     proxy.enable_ssl_verify(false);
        // });
    });

    router->GET("/aaa", [ctx](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        co_return resp.proxy("https://127.0.0.1:8888/video", [] (manapi::net::fetch &proxy) -> void {
            proxy.enable_alpn(false);
            proxy.enable_http2();
            proxy.enable_ssl_verify(false);
        });
    });

    router->GET ("/test", [](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {

        co_return resp.json({{"hello", "world"}, {"auaai", 78}, {"hello2", nullptr}});
    });

    router->POST ("/test", [](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        co_await req.file("./test.empty");
        // resp.set_header(manapi::net::HTTP_HEADER.CONTENT_TYPE, manapi::net::HTTP_MIME.TEXT_PLAIN);
        // auto formdata = co_await req.form();
        // ssize_t size = 298512394;
        // ssize_t fsize = 0;
        // std::string data;
        // while (true) {
        //     if (formdata.next_file()) {
        //         std::cout << formdata.about_file().param_name << " " << formdata.about_file().file_name << " " << formdata.about_file().mime_type << "\n";
        //         co_await formdata.save_file("/home/Timur/video2.mp4");
        //         fsize = manapi::filesystem::get_size("/home/Timur/video2.mp4");
        //         continue;
        //     }
        //     if (formdata.next_param()) {
        //         auto data = co_await formdata.get_param();
        //         if (data.first == "size") {
        //             size = std::stoll(data.second);
        //         }
        //         continue;
        //     }
        //
        //     break;
        // }
        // if (size != fsize) {
        //     co_return resp.text(std::format("{} {}", size, fsize));
        // }
        co_return resp.json({{"status", true ? "OK" : "ERROR"}});
    });

    router->POST ("/test2", [ctx](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        co_await manapi::async::delay{ctx, 50000};
        co_await req.file("/home/Timur/video2.mp4");
        resp.file ("/home/Timur/video2.mp4");
    });

    router->GET("/video", [](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        resp.set_partial_status(true);
        resp.set_compress_enabled(false);
        co_return resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
    });

    router->GET("/stop", [ctx](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        /* stop the app */
        co_await ctx->stop();
        co_return resp.text("stopped");
    });

    router->GET("/timeout", [ctx](decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        /* stop the app */
        co_await manapi::async::delay{ctx, 10000};
        co_return resp.text("10sec");
    });

    router->GET ("/bigfile", [] (decltype(router)::element_type::req req, decltype(router)::element_type::resp resp) -> manapi::future<> {
        resp.set_partial_status(false);
        resp.set_compress_enabled(false);

        co_return resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO");
    });

    manapi::async::run(ctx, [router, db] () -> manapi::future<> {
        co_await db->connect("127.0.0.1", "7879", "development", "rv8FY--PHz_QV<wvT4=n_Ru+cUJE}>KCqmBj9&#M3\\\"Gb.tx", "workflow-main");
        co_await router->start();
    });

    ctx->sync_start();

    return 0;
}





//
// #include <iostream>
// #include <csignal>
// #include <fstream>
// #include <zlib.h>
// #include <coroutine>
// #include <fcntl.h>
// #include <features.h>
// #include <thread>
//
// #include "ManapiHttp.hpp"
// #include "ManapiFilesystem.hpp"
// #include "include/services/ManapiTaskFunction.hpp"
// #include "include/services/ManapiFetch.hpp"
// #include "ManapiJsonBuilder.hpp"
// #include "ManapiJsonMask.hpp"
// #include "ManapiUnicode.hpp"
// #include "ManapiHttpMime.hpp"
// #include "compress/ManapiHPack.hpp"
// #include "ManapiAsync.hpp"
// #include "ManapiString.hpp"
// #include "async/ManapiAsyncContext.hpp"
// #include "async/ManapiAsyncConditionVariable.hpp"
// #include "async/ManapiAsyncContext.hpp"
// #include "async/ManapiAsyncMutex.hpp"
// #include "async/ManapiAsyncParallelRun.hpp"
// #include "components/ManapiChain.hpp"
// #include "crypto/ManapiAEAD.hpp"
// #include "crypto/ManapiAES.hpp"
// #include "extensions/pq/AsyncPostgreClient.hpp"
// #include "extensions/pq/AsyncPostgreValue.hpp"
// #include "services/ManapiEventLoop.hpp"
// #include "worker/tools/OpenSSLTools.hpp"
// #include <memory.h>
//
// using namespace manapi::net;
//
// using namespace std;
//
//
//
// int main (int argc, char *argv[]) {
//     worker::tools::ssl_library_init();
//     worker::tools::ev_library_init();
//     manapi::debug::debug_print_memory("start");
//
//     {
//         auto ctx = manapi::async::context::create(std::thread::hardware_concurrency(), 0.01);
//         ctx->eventloop()->setup_handle_interrupt();
//
//         // manapi::async::run(ctx, [ctx] () -> manapi::future<> {
//         //             auto data = co_await manapi::filesystem::read_async(ctx, "/home/Timur/Desktop/WorkSpace/test/msg.txt");
//         //             std::cout << "recv: " << data << "\n";
//         //         });
//        // manapi::async::run(ctx, manapi::filesystem::write_async(ctx, "/home/Timur/Desktop/WorkSpace/test/msg.txt", "hello world! Helicopter 32", 0777));
//
//         {
//         http::server server (ctx);
//         server.set_config("./config.json");
//
//         server.GET ("/", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_compress_enabled(true);
//             resp.set_header(HTTP_HEADER.ALT_SVC, http::stringify_header_value({{"", {{"h3", "\":8888\""}, {"ma", "86400"}}}}));
//             resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
//             co_return;
//         });
//
//         server.GET ("/response", [&server] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             printf("3 sec later...\n");
//             co_await manapi::async::delay{server.async_context(), 5000};
//             resp.text("5 sec later...");
//             co_return;
//         });
//
//         server.GET ("/lenar", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_compress_enabled(false);
//             resp.file ("/opt/clion.zip");
//             co_return;
//         });
//
//         server.GET ("+layer", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_header("test", "test");
//             co_return;
//         });
//
//         server.OPTIONS("+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_header("allow", "OPTIONS, GET, POST");
//             resp.set_status(204, HTTP_STATUS.NO_CONTENT_204);
//             co_return;
//         });
//
//         server.GET ("/test2", [ctx] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             fetch response (ctx, "http://localhost:8889");
//             response.set_method("GET");
//             // response.set_body(json2form({
//             //     {"first-name", "Timur"},
//             //     {"last-name", "Zajnullin"},
//             //     {"file", "lol OK ??&?45=ersdf--  \\"}
//             // }));
//             response.enable_ssl_verify(false);
//             // response.set_custom_setup([] (CURL *curl) -> void {
//             //     curl_easy_setopt(curl, CURLOPT_HTTP_VERSION,
//             //          (long)CURL_HTTP_VERSION_3);
//             // });
//             resp.text(co_await response.text());
//             co_return;
//         });
//
//         server.GET ("/video", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_compress_enabled(false);
//             resp.set_partial_status(true);
//             resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);
//
//             resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
//             co_return;
//         });
//
//         // server.GET ("/bigfile", [] (REQ(req), RESP(resp)) {
//         //     resp.set_compress_enabled(false);
//         //     resp.set_partial_status(false);
//         //
//         //     resp.file("/home/Timur/Downloads/Фотосессия Иглино.zip");
//         // });
//
//         server.GET("/proxy", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             manapi::json jp = {
//                     {"error", false},
//                     {"message", "this is a list"},
//                     {"hello", nullptr}
//             };
//
//             try {
//                 jp["hello"] = req.get_query_param("hello");
//             }
//             catch (const manapi::exception &e)
//             {
//                 std::cerr << e.what() << "\n";
//             }
//
//             resp.set_compress_enabled(false);
//             resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JS + ";charset=UTF-8");
//
//             resp.json (jp, 4);
//             co_return;
//         });
//
//         server.POST ("/json", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             auto res = co_await req.json();
//             res[0]["hello"] = "world";
//             resp.json(std::move(res));
//         });
//
//         server.POST ("/file", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             co_await req.file("/home/Timur/test.docx");
//             co_return resp.text("OK");
//         });
//
//         server.POST ("/formdata", [&] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             formdata_send formdata (ctx);
//             formdata.append_text("hello", "world");
//             formdata.append_file("file", "/home/Timur/test.docx");
//             co_return resp.form(std::move(formdata));
//         });
//
//         server.POST ("/exit", [&] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             exit(-1);
//             co_return;
//         });
//
//         server.POST ("/test_fetch", [&] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             auto fetch = co_await fetch2::fetch(ctx, "https://localhost:8888/formdata");
//             resp.text(co_await fetch->text());
//         });
//
//         server.GET ("/text", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             MANAPIHTTP_LOG("{}", "REQ GET");
//             resp.set_compress_enabled(false);
//             resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.TEXT_PLAIN);
//
//             std::ifstream f ("/home/Timur/.p10k.zsh");
//
//             if (!f.is_open()) {
//                 resp.json({
//                     {"error", "could not open the file"}
//                 });
//
//                 co_return;
//             }
//
//             std::string content;
//             while (f) {
//                 std::string line;
//                 std::getline(f, line);
//                 content += line + "\n";
//             }
//
//             resp.text (content);
//             co_return;
//         });
//
//         server.GET ("/+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_replacers({
//                    {"status_code", std::to_string(resp.get_status_code())},
//                    {"status_message", resp.get_status_message()},
//                    {"url", "/noooo"}
//             });
//
//             resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
//             co_return;
//         });
//
//         server.POST ("/+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON);
//             resp.json({
//                               {"error", true},
//                               {"message", "just a error"}
//             });
//             co_return;
//         });
//
//         server.GET ("/noooo", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);
//             resp.set_partial_status(true);
//             resp.set_compress_enabled(false);
//
//             resp.file("/home/Timur/Downloads/VideoDownloader/no.mp4");
//             co_return;
//         });
//
//         server.GET ("/nonoo", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_partial_status(true);
//             resp.set_compress_enabled(false);
//             resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);
//
//             resp.file("/home/Timur/Downloads/VideoDownloader/nono.mp4");
//             co_return;
//         });
//
//         // server.GET ("/[filename]-[extension]", [](REQ(req), RESP(resp)) {
//         //     auto &filename     = req.get_param("filename");
//         //     auto &extension    = req.get_param("extension");
//         //
//         //     resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test/" + filename + '.' + extension);
//         // });
//         //
//         // server.GET ("/[filename]-[extension]/+error", [](REQ(req), RESP(resp)) {
//         //     resp.set_replacers({
//         //            {"status_code", std::to_string(resp.get_status_code())},
//         //            {"status_message", resp.get_status_message()},
//         //            {"url", "/nonoo"}
//         //    });
//         //
//         //     resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
//         // });
//         server.GET ("/bigfile", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             std::cout << req.dump() << '\n';
//             resp.set_partial_status(false);
//             resp.set_compress_enabled(false);
//
//             resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO");
//             co_return;
//         });
//
//         server.GET ("/мем4", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_partial_status(true);
//             resp.file ("/home/Timur/Downloads/video3.mp4");
//             co_return;
//         });
//
//         server.GET("мемs", "/home/Timur/Downloads/VideoDownloader");
//
//         server.GET ("/favicon.ico", [](REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.file("/home/Timur/Documents/leon.ico");
//             co_return;
//         });
//
//         server.GET("/test", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             // fetch fetch ("http://127.0.0.1:8887/response");
//             // fetch.set_method("GET");1
//             // resp.text(fetch.text());
//                 std::cout << req.get_header("test").size() << "\n";
//             co_return;
//         });
//
//
//         const manapi::json_mask form_mask = {
//             {"first-name", "{string(>=5 <50)}"},
//             {"last-name", "{string(>=5 <70)}"}
//         };
//
//         server.GET("/stop", [&server] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             co_await server.stop();
//
//             resp.text("OK");
//             co_return;
//         });
//
//         server.GET("/audio", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_partial_status(true);
//             resp.set_compress_enabled(true);
//             resp.set_compress("gzip");
//             resp.file("/home/Timur/Music/Death By Glamour.mp3");
//             co_return;
//         });
//
//         server.GET("/freeze", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.text(std::to_string(1));
//             co_return;
//         });
//
//         server.GET ("/largeheader", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.set_header("set-cookie", manapi::string::random(8000));
//             resp.text("hehehehe");
//             co_return;
//         });
//
//         server.POST ("/form", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             auto formData = co_await req.form();
//
//             resp.set_compress_enabled(false);
//
//             resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON + ";charset=UTF-8");
//
//             manapi::json obj = manapi::json::object();
//
//             do {
//                 if (formData.next_param()) {
//                     auto data = co_await formData.get_param();
//                     obj.insert(data.first, data.second);
//                     continue;
//                 }
//                 if (formData.next_file()) {
//                     auto data = formData.about_file();
//                     obj.insert(data.param_name, std::format("[binary({})]", data.file_name));
//                     co_await formData.get_file([] (const char *buff, const size_t &size) -> void {});
//                     continue;
//                 }
//                 break;
//             } while (true);
//             cout << obj.dump(2) << "\n";
//             resp.json (obj, 4);
//             co_return;
//         });
//
//         server.GET ("/music", [ctx](REQ(req), RESP(resp)) -> manapi::future<void> {
//             std::string response;
//             response += manapi::crypto::strdec2strhex(co_await manapi::crypto::random_string_async(ctx, 100)) + "<hr />";
//             for (const auto &file: std::filesystem::directory_iterator ("/home/Timur/Music")) {
//                 std::string filename = file.path().filename().string();
//                 response += std::format("<a href=\"/music/{}\">{}</a><br />", manapi::unicode::escape_string(filename), file.path().filename().string());
//             }
//             resp.text(response);
//             co_return;
//         });
//
//         server.GET("/music", "/home/Timur/Music");
//
//         server.GET ("/", "/home/Timur/Desktop/WorkSpace/oneworld/");
//
//         manapi::debug::debug_print_memory("pool");
//
//         manapi::async::run (ctx, server.start());
//
//              ctx->sync_start();
//
//         manapi::debug::debug_print_memory("preend");
//         }
//         // manapi::debug::debug_print_memory("preend 2");
//
//         auto &b = manapi::async::async_tasks;
//         printf("ASYNC STACK: %zi. ctx: %zi\n", b.size(), ctx.use_count());
//
//         manapi::debug::debug_print_memory("preend 8");
//     }
//
//     manapi::async::async_tasks = std::move(typeof (manapi::async::async_tasks){});
//
//     //sleep(2);
//
//     manapi::debug::debug_print_memory("end");
//
//     return 0;
// }
