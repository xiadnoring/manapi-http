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

std::shared_ptr<manapi::async::context> ctx;


manapi::future<> co_main () {
    manapi::ext::pq::connection db (ctx);
    db.set_notify_cb([] (manapi::ext::pq::notification notify) -> manapi::future<> {
        std::cout << notify.pid() << " " << notify.payload() << "\n";
        co_return;
    });
    co_await db.connect("127.0.0.1", "7879", "development", "rv8FY--PHz_QV<wvT4=n_Ru+cUJE}>KCqmBj9&#M3\\\"Gb.tx", "workflow-main");

    auto res_listen = co_await db.exec("LISTEN virtual;");

    std::string data = "hello world \007\010";
    manapi::ext::pq::blob blob {data};
    std::string textdata = "hello world";
    manapi::ext::pq::text text {textdata};
    co_await manapi::async::delay{ctx, 5s};
    manapi::ext::pq::result res;
    if (false) {
        try {
            res = co_await db.exec("INSERT INTO for_test (id, float_col, blob_col, str_col, bool_col, text_col) VALUES ($1, $2, $3, $4, $5, $6);", 6, 78.56, blob, "hello world #2", true, text);
        }
        catch (manapi::ext::pq::sqlexception const &e) {
            std::cout << e.sqlstate() << " " << e.what() << "\n";
            co_return;
        }
    }
    else {
        res = co_await db.exec("SELECT * FROM for_test");
        std::cout << res.size() << "\n";
        for (const auto &row: res) {
            cout << "id: " << row["id"].as<int>() << "\nfloat_col: " << row["float_col"].as<long double>() << "\nblob_col: " << row["blob_col"].as<std::string>();
            cout << "\ntext_col: " << row["text_col"].as<std::string>() << "\nbool_col: " << row["bool_col"].as<bool>() << "\nstr_col: " << row["str_col"].as<std::string>() << "\n";
        }
    }
    std::cout << res.affected_rows() << "\n";
    db.close();
    co_return;
}

int main () {
    ctx = manapi::async::context::create();
    ctx->eventloop()->setup_handle_interrupt();
    manapi::async::run(ctx, co_main());
    ctx->sync_start();
    return 0;
}


// int main (int argc, char *argv[]) {
//     worker::tools::ssl_library_init();
//
//     manapi::debug::debug_print_memory("start");
//     std::string data = "hello world test 2";
//     {
//         auto ctx = manapi::async::context::create();
//         ctx->eventloop()->setup_handle_interrupt();
//         // manapi::async::run(ctx, [ctx] () -> manapi::future<> {
//         //             auto data = co_await manapi::filesystem::read_async(ctx, "/home/Timur/Desktop/WorkSpace/test/msg.txt");
//         //             std::cout << "recv: " << data << "\n";
//         //         });
//        // manapi::async::run(ctx, manapi::filesystem::write_async(ctx, "/home/Timur/Desktop/WorkSpace/test/msg.txt", "hello world! Helicopter 32", 0777));
//
//         {
//             http::server server (ctx);
//             server.set_config("./config.json");
//
//             server.GET ("/", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_compress_enabled(true);
//                 resp.set_header(HTTP_HEADER.ALT_SVC, http::stringify_header_value({{"", {{"h3", "\":8888\""}, {"ma", "86400"}}}}));
//                 resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/index.html");
//                 co_return;
//             });
//
//             server.GET ("/response", [&server] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 printf("3 sec later...\n");
//                 co_await manapi::async::delay{server.async_context(), 5s};
//                 resp.text("5 sec later...");
//                 co_return;
//             });
//
//             server.GET ("/lenar", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_compress_enabled(false);
//                 resp.file ("/opt/clion.zip");
//                 co_return;
//             });
//
//             server.GET ("+layer", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_header("test", "test");
//                 co_return;
//             });
//
//             server.OPTIONS("+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_header("allow", "OPTIONS, GET, POST");
//                 resp.set_status(204, HTTP_STATUS.NO_CONTENT_204);
//                 co_return;
//             });
//
//             server.GET ("/test2", [&server] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 fetch response ("http://localhost:8889", server);
//                 response.set_method("GET");
//                 // response.set_body(json2form({
//                 //     {"first-name", "Timur"},
//                 //     {"last-name", "Zajnullin"},
//                 //     {"file", "lol OK ??&?45=ersdf--  \\"}
//                 // }));
//                 response.enable_ssl_verify(false);
//                 // response.set_custom_setup([] (CURL *curl) -> void {
//                 //     curl_easy_setopt(curl, CURLOPT_HTTP_VERSION,
//                 //          (long)CURL_HTTP_VERSION_3);
//                 // });
//                 resp.text(co_await response.text());
//                 co_return;
//             });
//
//             server.GET ("/video", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_compress_enabled(false);
//                 resp.set_partial_status(true);
//                 resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);
//
//                 resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
//                 co_return;
//             });
//
//             // server.GET ("/bigfile", [] (REQ(req), RESP(resp)) {
//             //     resp.set_compress_enabled(false);
//             //     resp.set_partial_status(false);
//             //
//             //     resp.file("/home/Timur/Downloads/Фотосессия Иглино.zip");
//             // });
//
//             server.GET("/proxy", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 manapi::json jp = {
//                         {"error", false},
//                         {"message", "this is a list"},
//                         {"hello", nullptr}
//                 };
//
//                 try {
//                     jp["hello"] = req.get_query_param("hello");
//                 }
//                 catch (const manapi::exception &e)
//                 {
//                     std::cerr << e.what() << "\n";
//                 }
//
//                 resp.set_compress_enabled(false);
//                 resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JS + ";charset=UTF-8");
//
//                 resp.json (jp, 4);
//                 co_return;
//             });
//
//             server.GET ("/text", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 MANAPIHTTP_LOG("{}", "REQ GET");
//                 resp.set_compress_enabled(false);
//                 resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.TEXT_PLAIN);
//
//                 std::ifstream f ("/home/Timur/.p10k.zsh");
//
//                 if (!f.is_open()) {
//                     resp.json({
//                         {"error", "could not open the file"}
//                     });
//
//                     co_return;
//                 }
//
//                 std::string content;
//                 while (f) {
//                     std::string line;
//                     std::getline(f, line);
//                     content += line + "\n";
//                 }
//
//                 resp.text (content);
//                 co_return;
//             });
//
//             server.GET ("/+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_replacers({
//                        {"status_code", std::to_string(resp.get_status_code())},
//                        {"status_message", resp.get_status_message()},
//                        {"url", "/noooo"}
//                 });
//
//                 resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
//                 co_return;
//             });
//
//             server.POST ("/+error", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON);
//                 resp.json({
//                                   {"error", true},
//                                   {"message", "just a error"}
//                 });
//                 co_return;
//             });
//
//             server.GET ("/noooo", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);
//                 resp.set_partial_status(true);
//                 resp.set_compress_enabled(false);
//
//                 resp.file("/home/Timur/Downloads/VideoDownloader/no.mp4");
//                 co_return;
//             });
//
//             server.GET ("/nonoo", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_partial_status(true);
//                 resp.set_compress_enabled(false);
//                 resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.VIDEO_MP4);
//
//                 resp.file("/home/Timur/Downloads/VideoDownloader/nono.mp4");
//                 co_return;
//             });
//
//             // server.GET ("/[filename]-[extension]", [](REQ(req), RESP(resp)) {
//             //     auto &filename     = req.get_param("filename");
//             //     auto &extension    = req.get_param("extension");
//             //
//             //     resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test/" + filename + '.' + extension);
//             // });
//             //
//             // server.GET ("/[filename]-[extension]/+error", [](REQ(req), RESP(resp)) {
//             //     resp.set_replacers({
//             //            {"status_code", std::to_string(resp.get_status_code())},
//             //            {"status_message", resp.get_status_message()},
//             //            {"url", "/nonoo"}
//             //    });
//             //
//             //     resp.file ("/home/Timur/Desktop/WorkSpace/oneworld/error.html");
//             // });
//             server.GET ("/bigfile", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 std::cout << req.dump() << '\n';
//                 resp.set_partial_status(false);
//                 resp.set_compress_enabled(false);
//
//                 resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO");
//                 co_return;
//             });
//
//             server.GET ("/мем4", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_partial_status(true);
//                 resp.file ("/home/Timur/Downloads/video3.mp4");
//                 co_return;
//             });
//
//             server.GET("мемs", "/home/Timur/Downloads/VideoDownloader");
//
//             server.GET ("/favicon.ico", [](REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.file("/home/Timur/Documents/leon.ico");
//                 co_return;
//             });
//
//             server.GET("/test", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 // fetch fetch ("http://127.0.0.1:8887/response");
//                 // fetch.set_method("GET");1
//                 // resp.text(fetch.text());
//                     std::cout << req.get_header("test").size() << "\n";
//                 co_return;
//             });
//
//
//             const manapi::json_mask form_mask = {
//                 {"first-name", "{string(>=5 <50)}"},
//                 {"last-name", "{string(>=5 <70)}"}
//             };
//
//             server.GET("/stop", [&server] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 co_await server.stop();
//
//                 resp.text("OK");
//                 co_return;
//             });
//
//             server.GET("/audio", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_partial_status(true);
//                 resp.set_compress_enabled(true);
//                 resp.set_compress("gzip");
//                 resp.file("/home/Timur/Music/Death By Glamour.mp3");
//                 co_return;
//             });
//
//             server.GET("/freeze", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.text(std::to_string(1));
//                 co_return;
//             });
//
//             server.GET ("/largeheader", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 resp.set_header("set-cookie", manapi::string::random(8000));
//                 resp.text("hehehehe");
//                 co_return;
//             });
//
//             server.POST ("/form", [] (REQ(req), RESP(resp)) -> manapi::future<void> {
//                 auto formData = co_await req.form();
//
//                 resp.set_compress_enabled(false);
//
//                 resp.set_header(HTTP_HEADER.CONTENT_TYPE, HTTP_MIME.APPLICATION_JSON + ";charset=UTF-8");
//
//                 manapi::json obj = manapi::json::object();
//
//                 do {
//                     if (formData.next_param()) {
//                         auto data = co_await formData.get_param();
//                         obj.insert(data.first, data.second);
//                         continue;
//                     }
//                     if (formData.next_file()) {
//                         auto data = formData.about_file();
//                         obj.insert(data.param_name, std::format("[binary({})]", data.file_name));
//                         co_await formData.get_file([] (const char *buff, const size_t &size) -> void {});
//                         continue;
//                     }
//                     break;
//                 } while (true);
//                 cout << obj.dump(2) << "\n";
//                 resp.json (obj, 4);
//                 co_return;
//             });
//
//             server.GET ("/music", [ctx](REQ(req), RESP(resp)) -> manapi::future<void> {
//                 std::string response;
//                 response += manapi::crypto::strdec2strhex(co_await manapi::crypto::random_string_async(ctx, 100)) + "<hr />";
//                 for (const auto &file: std::filesystem::directory_iterator ("/home/Timur/Music")) {
//                     std::string filename = file.path().filename().string();
//                     response += std::format("<a href=\"/music/{}\">{}</a><br />", manapi::unicode::escape_string(filename), file.path().filename().string());
//                 }
//                 resp.text(response);
//                 co_return;
//             });
//
//             server.GET("/music", "/home/Timur/Music");
//
//             server.GET ("/", "/home/Timur/Desktop/WorkSpace/oneworld/");
//
//             manapi::debug::debug_print_memory("pool");
//
//             manapi::async::run (ctx, server.start());
//
//             ctx->sync_start();
//
//             manapi::debug::debug_print_memory("preend");
//         }
//         manapi::debug::debug_print_memory("preend 2");
//
//         auto &b = manapi::async::async_tasks;
//         printf("ASYNC STACK: %zi\n", b.size());
//
//         manapi::async::async_tasks = {};
//         manapi::debug::debug_print_memory("preend 8");
//     }
//
//     manapi::debug::debug_print_memory("end");
//
//     return 0;
// }
