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
// #include "extensions/pq/AsyncPostgreClient.hpp"
// #include "extensions/pq/AsyncPostgreValue.hpp"
#include "services/ManapiEventLoop.hpp"
#include "services/ManapiFetch2.hpp"
#include <memory.h>
#include <pg_config.h>
#include <libpq/libpq-fs.h>

#include "ManapiHash.hpp"
#include "ManapiMath.hpp"
#include "ManapiProcess.hpp"
//#include "extensions/pq/AsyncPostgreClient.hpp"

//SO_ATTACH_REUSEPORT_CBPF

manapi::future<int> test () {
    co_return 1;
}

int main () {
    int threads = 2;
    try { threads = std::stoi(manapi::process::get_env("MANAPIHTTP_THREADS")); }
    catch (...) {  }

    manapi::async::context::threadpoolfs(threads);
    manapi::async::context::gbs = manapi::async::context::blockedsignals();

    int loops = 2;
    try { loops = std::stoi(manapi::process::get_env("MANAPIHTTP_LOOPS")); }
    catch (...) {  }
    GCTX_OBJ = manapi::async::context::create(0, loops);
    GCTX_OBJ->eventloop()->setup_handle_interrupt();

    manapi::async::cthread::current(GCTX_OBJ);

    auto mx = std::make_shared<manapi::async::mutex>();
    GCTX_OBJ->logger()->callback([mx](manapi::logger_type type, std::string_view service, int error_code, std::string msg)
        -> void {
        manapi::async::run(manapi::async::invoke(+[](std::shared_ptr<manapi::async::mutex> mx, manapi::logger_type type, std::string_view service, int error_code, std::string msg) -> manapi::future<> {
            auto lk = co_await mx->lock_guard();
            if (type == manapi::logger_type::LOGGER_ERROR) {
                std::cerr << "[" << service.substr(1) << "][" << error_code << "]: " << msg << "\n";
            }
            else {
                std::cout << "[" << service.substr(1) << "][" << error_code << "]: " << msg << "\n";
            }
            co_return;
        }, mx, type, service, error_code, std::move(msg)));
    });

    //auto db = std::make_shared<manapi::ext::pq::connection>(GCTX_OBJ);

    manapi::net::http::server router;

    std::atomic<int> a = 0;

    router.GET ("/", [&a] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        a.fetch_add(1);
        resp.compress_enabled(false);
        co_return resp.text("");
    });

    router.GET ("/stat", [&a] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        std::cout << "/stat\n";
        resp.compress_enabled(false);
        co_return resp.text(std::to_string(a.load()));
    });

    router.GET ("/favicon.ico", [&a] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        resp.compress_enabled(false);
        co_return resp.text("no");
    });

    // router.GET ("/zstd", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
    //     -> manapi::future<> {
    //     resp.compress("zstd");
    //     resp.compress_enabled(true);
    //     co_return resp.file("./test.html");
    // });

    // router.GET ("/brotli", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
    //     -> manapi::future<> {
    //     resp.compress("br");
    //     resp.compress_enabled(true);
    //     co_return resp.file("./test.html");
    // });
    //
    // router.GET ("/http-test", [cnt = std::make_shared<std::atomic<int>>(0)] (manapi::net::http::request &req, manapi::net::http::response &resp) mutable
    //     -> manapi::future<> {
    //     resp.compress_enabled(false);
    //     co_return resp.text(std::to_string(cnt->fetch_add(1)));
    // });
    //
    // router.GET("/random", [] (manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
    //     try {
    //         std::string data;
    //         data.resize(64);
    //         manapi::async::cancellation_action cancellation (GCTX_OBJ);
    //         cancellation.timeout(5000);
    //         co_await manapi::crypto::async_random_string(GCTX(data.data(), data.size(), std::move(cancellation)));
    //         data = manapi::crypto::strdec2strhex(data);
    //         co_return resp.text(std::move(data));
    //     }
    //     catch (...) {
    //         co_return resp.text("error");
    //     }
    // });
    //
    // router.POST ("/upload", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
    //     -> manapi::future<> {
    //     ssize_t result = 0;
    //     std::cout << "start\n";
    //     try {
    //         co_await req.callback_sync([&result] (const char *buffer, ssize_t size)
    //             -> ssize_t { result += size; return result; });
    //     }
    //     catch (std::exception const &e) {
    //         std::cout << e.what() << "\n";
    //     }
    //     std::cout << "end\n";
    //     co_return resp.text(std::format("{} : {}", result, result));
    // });
    //
    // router.GET("/download", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
    //     -> manapi::future<> {
    //     co_return resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO");
    // });
    //
    router.GET("/video", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        resp.compress_enabled(false);
        resp.partial_enabled(true);
        co_return resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
    });
    //
    // router.GET("/folder", "/home/Timur/Downloads/VideoDownloader");

    // router.GET("/pq/[id]", [db, mx = std::make_shared<manapi::async::mutex>(GCTX_OBJ)](manapi::net::http::request& req, manapi::net::http::response& resp) -> manapi::future<> {
    //     auto lk = co_await mx->lock_guard();
    //     /* The pool of database connections here / This example is so slow */
    //     try {
    //         auto res = co_await db->exec("INSERT INTO for_test (id, str_col) VALUES ($2, $1);","no way", std::stoll(req.param("id")));
    //     }
    //     catch (...) {
    //
    //     }
    //
    //     auto res = co_await db->exec("SELECT * FROM for_test;");
    //     lk.call();
    //
    //     std::string content = "b";
    //     for (const auto &row: res) {
    //         content += std::to_string(row["id"].as<int>()) + " - " + row["str_col"].as<std::string>() + "<hr/>";
    //     }
    //
    //     co_return resp.text(std::move(content));
    // });

    manapi::async::run([router] () mutable -> manapi::future<> {
        //co_await db->connect("127.0.0.1", "7879", "development", "rv8FY--PHz_QV<wvT4=n_Ru+cUJE}>KCqmBj9&#M3\\\"Gb.tx", "workflow-main");

        co_await router.config("./config.json");
        co_await router.start(GCTX_OBJ->loops());
    });

    GCTX_OBJ->sync_start();
}

// using namespace std;
// using namespace manapi::net;
// int main (int argc, char *argv[]) {
//     manapi::debug::debug_print_memory("start");
//
//
//     {
//         auto ctx = manapi::async::context::create(16, 0.01);
//         ctx->eventloop()->setup_handle_interrupt();
//
//
//         auto task = std::make_unique<manapi::function_task> ([] ()
//             -> void { std::cout << "Hello World\n"; });
//         ctx->taskpool()->append_task(std::move(task));
//
//         http::server server (ctx);
//         server.config("./config.json");
//
//         server.GET ("/", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
//             co_return;
//         }, nullptr, nullptr);
//
//         server.GET ("/http-test", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.text("hello world");
//             co_return;
//         }, nullptr, nullptr);
//
//         server.GET ("/test78", [&server, ctx] (REQ(req), RESP(resp)) -> manapi::future<void> {
//
//             co_await ctx->timerpool()->async_append_timer_async(500, [ctx] (manapi::timer t) -> manapi::future<> {
//                 /**
//                  * called in the event loop thread.
//                  * from other threads (which aren't an event loop) it will be undefined behaviour (UB)
//                  */
//                 co_await ctx->timerpool()->async_append_interval_sync(500, [ctx, index = 0] (manapi::timer t) mutable
//                     -> void { std::cout << std::format("interval: cnt {}\n", index++); });
//             });
//
//             co_return resp.text("timer has been added");
//         });
//
//         server.GET ("/lenar", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.compress_enabled(false);
//             resp.file ("/opt/clion.zip");
//             co_return;
//         });
//
//         server.GET ("+layer", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.header("test", "test");
//             co_return;
//         });
//
//         server.OPTIONS("+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.header("allow", "OPTIONS, GET, POST");
//             resp.status(204);
//             co_return;
//         });
//
//         server.GET ("/test2", [ctx] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             fetch response (ctx, "http://localhost:8889");
//             response.method("GET");
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
//             resp.compress_enabled(false);
//             resp.partial_enabled(true);
//             resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.VIDEO_MP4);
//
//             resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
//             co_return;
//         });
//
//         server.POST("/custom-cb", [&] (REQ(req), RESP(resp)) -> manapi::future<> {
//             ssize_t s = 0;
//             for (auto &header : req.headers()) {
//                 std::cout << header.first << ": " << header.second << "\n";
//             }
//             std::string result;
//
//             co_await req.callback_sync([&result, &s] (const char *buffer, ssize_t size)
//                 -> ssize_t {
//                 s += size;
//                 result += std::format("result = {}</br>", size); return size;
//             });
//             result = std::to_string(s) + "<hr>" + result;
//             resp.text(std::move(result));
//         });
//
//         server.GET ("/test-custom-cb2", [ctx] (REQ(req), RESP(resp)) -> manapi::future<> {
//             if (!req.contains_get_param("file")) {
//                 co_return resp.text("msg: GET parameter 'file' not found in the URL");
//             }
//             manapi::filesystem::fstream fio (ctx, req.get("file"));
//             co_await fio.open(fio.FILE_READ);
//             if (!fio.is_open()) {
//                 co_return resp.text("hnnn");
//             }
//             ssize_t ff = 0;
//             auto response = co_await manapi::net::fetch2::fetch (ctx,  "https://localhost:8080", {
//                 {"alpn", true},
//                 {"http2", true},
//                 {"ssl_verify", false},
//                 {"method", "POST"},
//                 {"verbose", true},
//                 {"headers", {
//                         {"content-length", fio.total_size()},
//                     {"content-type", manapi::mime::types.TEXT_PLAIN}
//                 }}
//             }, [fio, &ff] (char *buffer, ssize_t size) mutable
//                 -> manapi::future<ssize_t> {
//                 auto rhs = co_await fio.read(buffer, size);
//                 ff+=rhs;
//                 if (rhs <= 0) {
//                     std::cout << "we are here: " << ff <<" ? " << fio.tellg() << "/" << fio.total_size()<<"\n";
//                 }
//                 co_return rhs;
//             });
//             std::cout << "skip\n";
//             co_return resp.text(co_await response.text());
//         }, {{"file", "{string|none}"}});
//
//         server.GET ("/ansar", "/home/Timur/ansar");
//
//         server.GET ("/test-custom-cb", [ctx] (REQ(req), RESP(resp)) -> manapi::future<> {
//             if (!req.contains_get_param("file")) {
//                 co_return resp.text("msg: GET parameter 'file' not found in the URL");
//             }
//             manapi::filesystem::fstream fio (ctx, req.get("file"));
//             co_await fio.open(fio.FILE_READ);
//             if (!fio.is_open()) {
//                 co_return resp.text("hnnn");
//             }
//             ssize_t ff = 0;
//             auto response = co_await manapi::net::fetch2::fetch (ctx,  "https://localhost:8888/custom-cb/", {
//                 {"alpn", false},
//                 {"http2", true},
//                 {"ssl_verify", false},
//                 {"method", "POST"},
//                 {"verbose", true},
//                 {"headers", {
//                         {"content-length", fio.total_size()},
//                     {"content-type", manapi::mime::types.TEXT_PLAIN}
//                 }}
//             }, [fio, &ff] (char *buffer, ssize_t size) mutable
//                 -> manapi::future<ssize_t> {
//                 auto rhs = co_await fio.read(buffer, size);
//                 ff+=rhs;
//                 if (rhs <= 0) {
//                     std::cout << "we are here: " << ff <<" ? " << fio.tellg() << "/" << fio.total_size()<<"\n";
//                 }
//                 co_return rhs;
//             });
//             std::cout << "skip\n";
//             co_return resp.text(co_await response.text());
//         }, {{"file", "{string|none}"}});
//
//         // server.GET ("/bigfile", [] (REQ(req), RESP(resp)) {
//         //     resp.compress_enabled(false);
//         //     resp.partial_enabled(false);
//         //
//         //     resp.file("/home/Timur/Downloads/Фотосессия Иглино.zip");
//         // });
//
//         server.POST("/sha256sum", [] (manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
//             manapi::net::hash::SHA256 sha256;
//
//             ssize_t s = 0;
//             sha256.init();
//             co_await req.callback_sync([&sha256, &s] (const void *buffer, ssize_t size)
//                 -> ssize_t {
//
//                 sha256.update(static_cast<const uint8_t *>(buffer), size);
//
//                 s += size;
//                 return size;
//             });
//
//             std::string diggest;
//             diggest.resize(32);
//             sha256.final(reinterpret_cast <uint8_t *>(diggest.data()));
//             auto a = manapi::crypto::strdec2strhex(std::move(diggest));
//             resp.text(std::format("hex={} size={}", a, s));
//         });
//
//         server.GET("/proxy", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             manapi::json jp = {
//                     {"error", false},
//                     {"message", "this is a list"},
//                     {"hello", nullptr}
//             };
//
//             try {
//                 jp["hello"] = req.get("hello");
//             }
//             catch (const manapi::exception &e)
//             {
//                 std::cerr << e.what() << "\n";
//             }
//
//             resp.compress_enabled(false);
//             resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.APPLICATION_JS + ";charset=UTF-8");
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
//             auto fetch = co_await manapi::net::fetch2::fetch(ctx, "https://localhost:8888/formdata");
//             resp.text(co_await fetch.text());
//         });
//
//         server.GET ("/text", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             MANAPIHTTP_LOG("{}", "REQ GET");
//             resp.compress_enabled(false);
//             resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.TEXT_PLAIN);
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
//             resp.replacers({
//                    {"status_code", std::to_string(resp.status_code())},
//                    {"status_message", std::string{resp.status_message()}},
//                    {"url", "/noooo"}
//             });
//
//             resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
//             co_return;
//         });
//
//         server.POST ("/+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.APPLICATION_JSON);
//             resp.json({
//                               {"error", true},
//                               {"message", "just a error"}
//             });
//             co_return;
//         });
//
//         server.GET ("/noooo", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.VIDEO_MP4);
//             resp.partial_enabled(true);
//             resp.compress_enabled(false);
//
//             resp.file("/home/Timur/Downloads/VideoDownloader/no.mp4");
//             co_return;
//         });
//
//         server.GET ("/nonoo", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.partial_enabled(true);
//             resp.compress_enabled(false);
//             resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.VIDEO_MP4);
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
//         //     resp.replacers({
//         //            {"status_code", std::to_string(resp.get_status_code())},
//         //            {"status_message", resp.get_status_message()},
//         //            {"url", "/nonoo"}
//         //    });
//         //
//         //     resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
//         // });
//         server.GET ("/bigfile", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             std::cout << req.dump() << '\n';
//             resp.partial_enabled(false);
//             resp.compress_enabled(false);
//
//             resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO");
//             co_return;
//         });
//
//         server.GET ("/мем4", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.partial_enabled(true);
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
//                 std::cout << req.header("test").size() << "\n";
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
//
//             resp.partial_enabled(true);
//             resp.compress_enabled(true);
//             resp.compress("gzip");
//             resp.file("/home/Timur/Music/Death By Glamour.mp3");
//             co_return;
//         });
//
//         server.GET("/pproxy", [&ctx](http::server::req &req, http::server::resp &resp) -> manapi::future<> {
//             auto fetch = co_await manapi::net::fetch2::fetch (ctx, "https://localhost:8888", {
//                 {"ssl_verify", false},
//                 {"alpn", false},
//                 {"method", "GET"},
//                 {"http1_1", true}
//             });
//
//             if (!fetch.ok()) {
//                 co_return resp.json ({{"error", true}, {"message", "fetch failed"}});
//             }
//
//             co_return resp.text(co_await fetch.text());
//         });
//
//         server.GET("/cat/[id]", [&ctx](http::server::req &req, http::server::resp &resp) -> manapi::future<> {
//             auto fetch = co_await manapi::net::fetch2::fetch (ctx, "https://dragonball-api.com/api/planets/" + req.param("id"), {
//                 {"enable_ssl_verify", false},
//                 {"enable_alpn", true},
//                 {"method", "GET"}
//             });
//
//             if (!fetch.ok()) {
//                 co_return resp.json ({{"error", true}, {"message", "fetch failed"}});
//             }
//
//             auto data = co_await fetch.json();
//
//             co_return resp.text(std::move(data["description"].as_string()));
//         });
//
//         server.GET("/download/[id]", [&ctx] (http::server::req &req, http::server::resp &resp) -> manapi::future<> {
//             co_return resp.proxy("http://www.fileconvoy.com/gf.php?id=g80195ca999d418791000586375.1836287cfc9eb241a43a68c&sts=17439984947779173102dccc3e2f7eed59ab7329055cde9bb6d0");
//         });
//
//         server.GET("/freeze", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.text(std::to_string(1));
//             co_return;
//         });
//
//         server.GET ("/largeheader", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             resp.header("set-cookie", manapi::string::random(8000));
//             resp.text("hehehehe");
//             co_return;
//         });
//
//         server.POST ("/form", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//             auto formData = co_await req.form();
//
//             resp.compress_enabled(false);
//
//             resp.header(http::HEADER.CONTENT_TYPE, manapi::mime::types.APPLICATION_JSON + ";charset=UTF-8");
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
//             response += manapi::crypto::strdec2strhex(co_await manapi::crypto::async_random_string(ctx, 100)) + "<hr />";
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
//         ctx->sync_start();
//
//         manapi::debug::debug_print_memory("preend");
//
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
