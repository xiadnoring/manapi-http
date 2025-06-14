#include "ManapiHttp.hpp"
#include "services/ManapiFetch2.hpp"
// #include "ext/pq/AsyncPostgreClient.hpp"
#ifdef _WIN32
#   define FOLDER ".\\data\\"
#else
//#   define FOLDER "/home/Timur/Downloads/anime-main/"
#endif
#define FOLDER "/home/Timur/Documents/http2priorities/"
#include <cstring>

#include "crypto/ManapiAEAD.hpp"
#include "ManapiHash.hpp"
#include "ManapiProcess.hpp"
#include "ManapiString.hpp"
#include "async/ManapiAsyncTimer.hpp"
#include "async/ManapiEasyCancellation.hpp"

//#include "extensions/pq/AsyncPostgreClient.hpp"

int main () {
    int threads = 2;
    try { threads = std::stoi(manapi::process::get_env("MANAPIHTTP_THREADS").value()); }
    catch (...) {  }

    manapi::async::context::threadpoolfs(threads);
    manapi::async::context::gbs = manapi::async::context::blockedsignals();

    int loops = 0;
    try { loops = std::stoi(manapi::process::get_env("MANAPIHTTP_LOOPS").value()); }
    catch (...) {  }

    auto ctx = manapi::async::context::create(loops);
    ctx->eventloop()->setup_handle_interrupt();

    auto mx = std::make_shared<manapi::async::tmutex>();
    ctx->logger()->callback(
        [mx = std::move(mx)](manapi::logger_type type, std::string_view service, int error_code, std::string msg)
        -> void {
        manapi::async::run(manapi::async::invoke(+[](std::shared_ptr<manapi::async::tmutex> mx, manapi::logger_type type, std::string_view service, int error_code, std::string msg) -> manapi::future<> {
            auto lk = co_await mx->lock_guard();
            (type == manapi::logger_type::LOGGER_ERROR ? std::cerr : std::cout)
                << "[" << service.substr(1) << "][" << error_code << "]: " << msg << "\n";
        }, mx, type, service, error_code, std::move(msg)));
    });


    std::atomic<int> a = 0;

    manapi::net::http::server_ctx server_ctx;

    manapi::async::context::run(ctx, loops, [&a, server_ctx] (const std::function<void()> &bind) -> void {

        using http = manapi::net::http::server;
        //manapi::ext::pq::connection db;
        manapi::net::http::server router (server_ctx);

        router.GET("/", FOLDER, [] (http::req &req, http::resp &resp)
            -> manapi::future<> {
            resp.compress_enabled(true);
            resp.compress("zstd");
            co_return;
        });

        router.GET("/", [] (http::req &req, http::resp &resp) -> manapi::future<> {
            co_return resp.file(FOLDER"index.html");
        });

        router.GET ("/main", [&a] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            std::cout << "MAIN PAGE\n";
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

        router.GET ("/zstd", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            resp.compress("zstd");
            resp.compress_enabled(true);
            co_return resp.file("./test.html");
        });

        router.GET ("/brotli", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            resp.compress("br");
            resp.compress_enabled(true);
            co_return resp.file("./test.html");
        });

        router.GET ("/gzip", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            resp.compress("gzip");
            resp.compress_enabled(true);
            co_return resp.file("./test.html");
        });

        router.GET ("/deflate", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            resp.compress("deflate");
            resp.compress_enabled(true);
            co_return resp.file("./test.html");
        });

        router.GET ("/http-test", [cnt = std::make_shared<std::atomic<int>>(0)] (manapi::net::http::request &req, manapi::net::http::response &resp) mutable
            -> manapi::future<> {
            co_return resp.text("");
        });

        router.GET("/random", [] (manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
            try {
                std::string data;
                data.resize(64);
                manapi::async::cancellation_action cancellation;
                cancellation.timeout(5000);
                co_await manapi::crypto::async_random_string(GCTX(data.data(), data.size(), std::move(cancellation)));
                data = manapi::crypto::strdec2strhex(data);
                co_return resp.text(std::move(data));
            }
            catch (...) {
                co_return resp.text("error");
            }
        });

        router.POST ("/formdata", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            ssize_t result = 0;
            manapi::net::hash::SHA256 hash{};
            hash.init();
            try {
                co_await req.form([&result, &hash] (std::string name) -> manapi::net::formdata_recv::ondata_cb_t {
                    return manapi::net::formdata_recv::save_file(std::move(name));
                });
            }
            catch (std::exception const &e) {
                std::cout << e.what() << "\n";
            }

            std::string b;
            b.resize(36);
            hash.final(reinterpret_cast<uint8_t *>(b.data()));

            b = manapi::crypto::strdec2strhex(b);

            std::cout << result << " " << b << "\n";

            co_return resp.text(std::format("{} : {}", result, b));
        });

        router.GET ("/chunked", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            try {
                auto cancellation = manapi::async::cancellation_action::unit(req.cancellation());
                cancellation.timeout(5000);
                cancellation.ask_cancel_callback();
                manapi::filesystem::fstream file ("/home/Timur/Downloads/VideoDownloader/ufa.mp4",
                    cancellation);
                co_await file.open (manapi::ev::FS_O_RDONLY|manapi::ev::FS_O_NONBLOCK);
                if (!file.is_open()) {
                    co_return resp.text("failed to open the file");
                }

                auto fetch = co_await manapi::net::fetch2::fetch("https://localhost:8885/upload", {
                    {"http", "2"},
                    {"verify_peer", false},
                    {"verbose", false},
                    {"alpn", false},
                    {"method", "POST"},
                    {"timeout", 5},
                    {"headers", {
                        //{"transfer-encoding", "chunked"}
                        {"content-length", "298512394"}
                    }}
                }, [file] (char *body, ssize_t size) mutable -> manapi::future<ssize_t> {
                    return file.read(body, size);
                }, manapi::async::cancellation_action::unit(cancellation));

                co_await file.close();

                if (!fetch.ok()) {
                    co_return resp.text(std::format("status : {}", fetch.status()));
                }
                co_return resp.text(co_await fetch.text());
            }
            catch (std::exception const &e) {
                co_return resp.text(e.what());
            }
        });

        router.POST ("/upload", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            ssize_t result = 0;
            manapi::net::hash::SHA256 hash{};
            hash.init();
            try {
                co_await req.callback_sync([&result, &hash] (const char *buffer, ssize_t size, bool fin)
                    -> ssize_t {
                    if (fin) {
                        std::cout << "FINSH\n";
                    }
                    hash.update(reinterpret_cast<const uint8_t *>(buffer), size);
                    result += size;
                    return size;
                });
            }
            catch (std::exception const &e) {
                std::cout << e.what() << "\n";
            }

            std::string b;
            b.resize(36);
            hash.final(reinterpret_cast<uint8_t *>(b.data()));

            b = manapi::crypto::strdec2strhex(b);

            std::cout << result << " " << b << "\n";

            co_return resp.text(std::format("{} : {}", result, b));
        });

        router.POST ("/upload_async", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            ssize_t result = 0;
            manapi::filesystem::fstream f ("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
            (co_await f.open(manapi::ev::FS_O_RDONLY)).throw_it();
            try {
                co_await req.callback_async([f, &result] (const char *buffer, ssize_t size, bool fin) mutable
                    -> manapi::future<ssize_t> {
                    if (fin) {
                        std::cout << "FINSH\n";
                    }
                    auto buffer2 = manapi::async::current()->memory_fabric().buffer(size);
                    co_await f.fread(buffer2.data(), size);
                    for (int i = 0; i < size; i++) {
                        assert(buffer[i] == buffer2[i]);
                    }
                    result += size;
                    co_return size;
                });
            }
            catch (std::exception const &e) {
                std::cout << e.what() << "\n";
            }


            co_return resp.text(std::format("{}", result));
        });

        router.POST ("/upload2", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            resp.header(manapi::net::http::HEADER.CONTENT_LENGTH, req.header(manapi::net::http::HEADER.CONTENT_LENGTH));
            co_return resp.callback_stream([&resp, &req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
                std::size_t sum = 0;
                co_await req.callback_async([&sum, cb = std::move(cb)] (const char *buffer, ssize_t size, bool fin) mutable
                    -> manapi::future<ssize_t> {
                    sum += size;
                    std::cout << sum << " " << size << " " << fin << "\n";
                    co_return co_await cb (buffer, size, fin);
                });
            });
        });

        router.GET("/download", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            co_return resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO");
        });

        router.GET("/video", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            std::cout<<"video send to "<<req.ip_data().ip <<":"<<(int)req.ip_data().port<<"\n";
            resp.compress_enabled(false);
            resp.partial_enabled(true);
            co_return resp.file("/home/Timur/Downloads/VideoDownloader/ufa.mp4");
        });

        router.GET("/timeout", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            std::cout << "wait\n";
            co_await manapi::async::delay{50000, manapi::async::cancellation_action::unit(req.cancellation())};
            std::cout << "YAY (cancelled?)\n";
            co_return resp.text("50000ms");
        });

        router.GET ("/ai", [] (http::req &req, http::resp &resp)
            -> manapi::future<> {
            if (!req.contains_get_param("text")) {
                co_return resp.text("GET param 'text' doesn't exists");
            }



            std::string ip = "https://localhost:8885/video";
            int timeout = 2000;
            if (req.contains_get_param("timeout")) {
                try {
                    timeout = std::stoi(req.get("timeout"));
                }
                catch (...) {

                }
            }
            if (req.contains_get_param("ip"))
                ip = req.get("ip");

            auto text = req.get("text");

            auto response = co_await manapi::net::fetch2::fetch(ip, {
                {"method", "GET"},
                {"http", "2"},
                {"verify_peer", false},
                {"alpn", false},
                {"verbose", true},
                {"headers", {
                    {"content-type", "application/json"},
                    {"authorization", "Bearer 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"}
                }}
            });

            co_return resp.text("yes");

            if (!response.ok()) {
                co_return resp.text(std::format("fetch failed. Http Status: {}", response.status()));
            }

            auto data = co_await response.text();
            co_return resp.text(std::move(data));
        });

        manapi::async::run([router] () mutable -> manapi::future<> {
           //co_await db.connect("127.0.0.1", "7879", "development", "rv8FY--PHz_QV<wvT4=n_Ru+cUJE}>KCqmBj9&#M3\\\"Gb.tx", "workflow-main");

            co_await router.config("config.json");
            co_await router.start();
        });

        bind();
    });
    return 0;
}
