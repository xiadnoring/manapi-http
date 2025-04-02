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
#include "include/encoding/ManapiUnicode.hpp"
#include "ManapiHttpMime.hpp"
#include "compress/ManapiHPack.hpp"
#include "ManapiAsync.hpp"
#include "ManapiString.hpp"
#include "async/ManapiAsyncContext.hpp"
#include "async/ManapiAsyncConditionVariable.hpp"
#include "async/ManapiAsyncContext.hpp"
#include "async/ManapiAsyncMutex.hpp"
#include "async/ManapiAsyncParallelRun.hpp"
#include "components/ManapiChain.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "crypto/ManapiAES.hpp"
#include "extensions/pq/AsyncPostgreClient.hpp"
#include "extensions/pq/AsyncPostgreValue.hpp"
#include "services/ManapiEventLoop.hpp"
#include "services/ManapiFetch2.hpp"
#include <memory.h>

#include "ManapiMath.hpp"

using namespace manapi::net;

using namespace std;



int main (int argc, char *argv[]) {
    manapi::debug::debug_print_memory("start");

    {
        auto ctx = manapi::async::context::create(std::thread::hardware_concurrency(), 0.01);
        ctx->eventloop()->setup_handle_interrupt();

        http::server server (ctx);
        server.config("./config.json");

        server.GET ("/", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            std::string msg;
            if (req.contains_get_param("hello")) {
                msg = req.get ("hello");
            }
            //resp.compress_enabled(true);
            resp.header(http::HEADER.ALT_SVC, http::stringify_header_value({{"", {{"h3", "\":8888\""}, {"ma", "86400"}}}}));
            if (msg.empty()) {
                resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
            }
            else {
                resp.text(std::format("You wrote: {}", msg));
            }
            co_return;
        }, {{"hello", "{string(<=100)}"}}, nullptr);

        server.GET ("/response", [&server] (REQ(req), RESP(resp)) -> manapi::future<void> {
            printf("3 sec later...\n");

            co_await manapi::async::delay{server.async_context(), 5000};
            resp.text("5 sec later...");
            co_return;
        });

        server.GET ("/lenar", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            resp.compress_enabled(false);
            resp.file ("/opt/clion.zip");
            co_return;
        });

        server.GET ("+layer", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            resp.header("test", "test");
            co_return;
        });

        server.OPTIONS("+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            resp.header("allow", "OPTIONS, GET, POST");
            resp.status(204);
            co_return;
        });

        server.GET ("/test2", [ctx] (REQ(req), RESP(resp)) -> manapi::future<void> {
            fetch response (ctx, "http://localhost:8889");
            response.method("GET");
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
            resp.compress_enabled(false);
            resp.partial_enabled(true);
            resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.VIDEO_MP4);

            resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
            co_return;
        });

        server.GET("/custom-cb", [&] (REQ(req), RESP(resp)) -> manapi::future<> {
            ssize_t size = 78;
            resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.TEXT_PLAIN);
            resp.header(http::HEADER.CONTENT_LENGTH, std::to_string(size));
            co_return resp.async_callback([ms = size, ctx] (char *buffer, ssize_t size, bool &finish) mutable -> manapi::future<ssize_t> {
                size = std::min(ms, size);
                std::string b = manapi::string::random(size);
                memcpy(buffer, b.data(), b.size());
                ms -= size;
                if (!ms) {
                    finish = true;
                }
                co_return size;
            });
        });

        // server.GET ("/bigfile", [] (REQ(req), RESP(resp)) {
        //     resp.compress_enabled(false);
        //     resp.partial_enabled(false);
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
                jp["hello"] = req.get("hello");
            }
            catch (const manapi::exception &e)
            {
                std::cerr << e.what() << "\n";
            }

            resp.compress_enabled(false);
            resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.APPLICATION_JS + ";charset=UTF-8");

            resp.json (jp, 4);
            co_return;
        });

        server.POST ("/json", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            auto res = co_await req.json();
            res[0]["hello"] = "world";
            resp.json(std::move(res));
        });

        server.POST ("/file", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            co_await req.file("/home/Timur/test.docx");
            co_return resp.text("OK");
        });

        server.POST ("/formdata", [&] (REQ(req), RESP(resp)) -> manapi::future<void> {
            formdata_send formdata (ctx);
            formdata.append_text("hello", "world");
            formdata.append_file("file", "/home/Timur/test.docx");
            co_return resp.form(std::move(formdata));
        });

        server.POST ("/exit", [&] (REQ(req), RESP(resp)) -> manapi::future<void> {
            exit(-1);
            co_return;
        });

        server.POST ("/test_fetch", [&] (REQ(req), RESP(resp)) -> manapi::future<void> {
            auto fetch = co_await manapi::net::fetch2::fetch(ctx, "https://localhost:8888/formdata");
            resp.text(co_await fetch->text());
        });

        server.GET ("/text", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            MANAPIHTTP_LOG("{}", "REQ GET");
            resp.compress_enabled(false);
            resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.TEXT_PLAIN);

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
            resp.replacers({
                   {"status_code", std::to_string(resp.status_code())},
                   {"status_message", std::string{resp.status_message()}},
                   {"url", "/noooo"}
            });

            resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
            co_return;
        });

        server.POST ("/+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.APPLICATION_JSON);
            resp.json({
                              {"error", true},
                              {"message", "just a error"}
            });
            co_return;
        });

        server.GET ("/noooo", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.VIDEO_MP4);
            resp.partial_enabled(true);
            resp.compress_enabled(false);

            resp.file("/home/Timur/Downloads/VideoDownloader/no.mp4");
            co_return;
        });

        server.GET ("/nonoo", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            resp.partial_enabled(true);
            resp.compress_enabled(false);
            resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.VIDEO_MP4);

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
        //     resp.replacers({
        //            {"status_code", std::to_string(resp.get_status_code())},
        //            {"status_message", resp.get_status_message()},
        //            {"url", "/nonoo"}
        //    });
        //
        //     resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
        // });
        server.GET ("/bigfile", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            std::cout << req.dump() << '\n';
            resp.partial_enabled(false);
            resp.compress_enabled(false);

            resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO");
            co_return;
        });

        server.GET ("/мем4", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            resp.partial_enabled(true);
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
                std::cout << req.header("test").size() << "\n";
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
            resp.partial_enabled(true);
            resp.compress_enabled(true);
            resp.compress("gzip");
            resp.file("/home/Timur/Music/Death By Glamour.mp3");
            co_return;
        });

        server.GET("/freeze", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            resp.text(std::to_string(1));
            co_return;
        });

        server.GET ("/largeheader", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            resp.header("set-cookie", manapi::string::random(8000));
            resp.text("hehehehe");
            co_return;
        });

        server.POST ("/form", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
            auto formData = co_await req.form();

            resp.compress_enabled(false);

            resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.APPLICATION_JSON + ";charset=UTF-8");

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

        // manapi::debug::debug_print_memory("preend 2");

        auto &b = manapi::async::async_tasks;
        printf("ASYNC STACK: %zi. ctx: %zi\n", b.size(), ctx.use_count());

        manapi::debug::debug_print_memory("preend 8");
    }

    manapi::async::async_tasks = std::move(typeof (manapi::async::async_tasks){});

    //sleep(2);

    manapi::debug::debug_print_memory("end");

    return 0;
}
