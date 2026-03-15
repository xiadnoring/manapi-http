#include <iostream>

#include <ManapiHttp.hpp>
#include <ManapiInitTools.hpp>

#include "ManapiFetch2.hpp"
#include "ManapiMath.hpp"
#include "ManapiString.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "hash/ManapiSHA256.hpp"

int main () {
    manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_MEDIUM);
    manapi::init_tools::log_name_enable("manapihttp", false);
    /* creates 2 threads for blocking I/O syscalls */
    manapi::async::context::threadpoolfs(2);
    /* disable several signals */
    manapi::async::context::gbs (manapi::async::context::blockedsignals());
    /* creates 4 additional threads for 4 additional event loops */
    auto ctx = manapi::async::context::create(4).unwrap();
    /* HTTP context for multiple HTTP routers (threadsafe) */
    auto router_ctx = manapi::net::http::server_ctx::create().unwrap();
    /* metric */
    std::atomic<int> cnt = 0;
    /* runs main event loop and 4 additional event loops */
    ctx->run(4, [&cnt, router_ctx] (std::function<void()> bind) -> void {
        using http = manapi::net::http::server;

        auto router = manapi::net::http::server::create(router_ctx).unwrap();

        router.GET ("/", [&cnt] (http::req &req, http::uresp resp) mutable -> void {
            resp->text(std::format("Hello World! Protocol:HTTP/{} Count: {}",
                manapi::net::http::config::stringify_http_version(req.http()), cnt.fetch_add(1))).unwrap();
            resp.finish();
        }).unwrap();
        //
        // router.GET("/+error", [](http::req &req, http::resp &resp) -> manapi::future<> {
        //     resp.replacers({
        //         {"status_code", std::to_string(resp.status_code())},
        //         {"status_message", std::string{resp.status_message()}}
        //     }).unwrap();
        //
        //     co_return resp.file ("../examples/error.html").unwrap();
        // }).unwrap();

        router.POST("/+error", [](http::req &req, http::resp &resp) -> manapi::future<> {
            co_return resp.json({{"error", resp.status_code()},
                    {"msg", std::string{resp.status_message()}}}).unwrap();
        }).unwrap();

        router.GET("/cat", [](http::req &req, http::resp &resp) -> manapi::future<> {
            auto fetch = manapi::unwrap(co_await manapi::net::fetch2::fetch (
                std::format("https://dragonball-api.com/api/planets/{}", manapi::math::random(0, 25)), {
                {"verify_peer", false},
                {"alpn", true},
                {"method", "GET"}
            }));

            if (!fetch.ok()) {
                co_return resp.json ({{"error", true}, {"message", "fetch failed"}}).unwrap();
            }

            auto data = manapi::unwrap(co_await fetch.json());

            co_return resp.text(std::move(data["description"].as_string())).unwrap();
        }).unwrap();

        router.GET("/proxy", [](http::req &req, http::resp &resp) -> manapi::future<> {
            resp.header(std::string{manapi::net::http::H_CONTENT_TYPE}, std::string{manapi::mime::types.VIDEO_MP4});
            co_return resp.proxy("http://127.0.0.1:8889/video").unwrap();
        }).unwrap();

        router.GET ("/largeheader", [] (http::req &req, http::resp &resp) -> manapi::future<void> {
            resp.header("set-cookie", manapi::string::random(8000));
            resp.text("hehehehe");
            co_return;
        });



        router.GET("/video", [](http::req &req, http::resp &resp) -> manapi::future<> {
            resp.partial_enabled(true);
            resp.compress_enabled(false);
            co_return resp.file("/home/Timur/Downloads/VideoDownloader/china.mp4").unwrap();
        }).unwrap();

        router.GET("/f", "/home/Timur/Downloads/VideoDownloader", [](http::req &req, http::resp &resp) -> manapi::future<> {
            resp.partial_enabled(true);
            resp.compress_enabled(false);
            co_return;
        }).unwrap();

        router.GET("/stop", [](http::req &req, http::resp &resp) -> manapi::future<> {
            /* stop the app */
            co_await manapi::async::current()->stop();
            co_return resp.text("stopped").unwrap();
        }).unwrap();

        router.GET("/timeout", [](http::req &req, http::resp &resp) -> manapi::future<> {
            /* stop the app */
            co_await manapi::async::delay{10000};
            co_return resp.text("10sec").unwrap();
        }).unwrap();

        router.GET("/noise", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            ssize_t len = 10737418240 / 2;

            resp.header(std::string{manapi::net::http::H_CONTENT_LENGTH}, std::to_string(len));
            co_return resp.callback_sync([current = (ssize_t)0, len] (char *buffer, ssize_t size, bool &flg) mutable
                    -> ssize_t {
                size = std::min(size, len - current);
                memset(buffer, '\0', size);
                len -= size;
                if (!len)
                    flg = true;
                return size;
            }).unwrap();
        });

        router.GET("/noise/[size]", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            ssize_t len = std::stoll(std::string{req.param("size").unwrap()});

            resp.header(std::string{manapi::net::http::H_CONTENT_LENGTH}, std::to_string(len));
            co_return resp.callback_sync([current = (ssize_t)0, len] (char *buffer, ssize_t size, bool &flg) mutable
                    -> ssize_t {
                size = std::min(size, len - current);
                memset(buffer, '\0', size);
                len -= size;
                if (!len)
                    flg = true;
                return size;
            }).unwrap();
        });

        router.GET("/+error", [] (manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
            resp.replacers({
                {"status_code", std::to_string(resp.status_code())},
                {"status_message", std::string{resp.status_message()}}
            });

            co_return resp.file("/home/Timur/Desktop/WorkSpace/ManapiHTTP/examples/error.html").unwrap();
        });

        router.POST ("/uploadtest", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            co_return resp.callback_stream([&req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
                ssize_t result = 0;
                auto c = std::chrono::steady_clock::now();
                try {
                    co_await req.callback_sync([&c, &result, &cb] (const char *buffer, ssize_t size, bool fin)
                        -> ssize_t {
                        result += size;
                        if (c + std::chrono::seconds (1) <= std::chrono::steady_clock::now()) {
                            auto a = std::format("{}\n", (double)result / 1024 / 1024);
                            result = 0;
                            c = std::chrono::steady_clock::now();
                            std::cout << a << "\n";
                        }
                        return size;
                    });
                }
                catch (std::exception const &e) {
                    std::cout << e.what() << "\n";
                }
                auto bb = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - c);
                std::string err = std::format("{}\n", ((double)result / 1024 / 1024) / ((double)bb.count()/1000));
                auto b = manapi::slice::create(err.size()).unwrap();
                b.copy_from(err.data(), 0, err.size());
                co_await cb(b, true);
            }).unwrap();
        });


        router.GET("/download", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            co_return resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO").unwrap();
        });

        router.POST ("/echo", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            if (req.contains_header(std::string{manapi::net::http::H_CONTENT_LENGTH}))
                resp.header(std::string{manapi::net::http::H_CONTENT_LENGTH},
                    std::string{req.header(std::string{manapi::net::http::H_CONTENT_LENGTH}).unwrap()});

            std::size_t sss = 0;
            co_return resp.callback_stream([&sss, &resp, &req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
                auto fs = manapi::fs::fstream::create ("/home/Timur/Downloads/VideoDownloader/ufa.mp4").unwrap();
                auto rhs = co_await fs.open(manapi::ev::FS_O_RDONLY);
                rhs.unwrap();
                co_await req.callback_async([&sss, cb = std::move(cb), fs] (manapi::slice_view buffs, bool fin) mutable
                    -> manapi::future<ssize_t> {
                    auto buffs2 = manapi::async::current()->memory_fabric().slice(buffs.size()).unwrap();
                    buffs2.resize(buffs.size());
                    assert(buffs.size() == buffs2.size());
                    auto res = co_await fs.fread(buffs2);
                    assert(res == buffs.size());
                    auto cmp = buffs.cmp(buffs2);
                    assert(!cmp);

                    //sum += size;
                    //std::cout << sum << " " << size << " " << fin << "\n";
                    auto result = co_await cb (buffs, fin);
                    sss+=result;
                    fs.seekg(fs.tellg() - buffs2.size() + result);
                    co_return result;
                });
                std::cout << sss << "\n";
            }).unwrap();
        });

        router.POST ("/uploadasynctest", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            co_return resp.callback_stream([&req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
                ssize_t result = 0;
                auto c = std::chrono::steady_clock::now();
                try {
                    co_await req.callback_async([&c, &result, &cb] (manapi::slice_view buffs, bool fin)
                        -> manapi::future<ssize_t> {
                        result += buffs.size();
                        if (c + std::chrono::seconds (1) <= std::chrono::steady_clock::now()) {
                            auto a = std::format("{}\n", (double)result / 1024 / 1024);
                            result = 0;
                            c = std::chrono::steady_clock::now();
                            std::cout << a << " " << buffs.size() << "\n";
                        }
                        co_return buffs.size();
                    });
                }
                catch (std::exception const &e) {
                    std::cout << e.what() << "\n";
                }
                auto bb = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - c);
                std::string err = std::format("{}\n", ((double)result / 1024 / 1024) / ((double)bb.count()/1000));
                auto b =manapi::slice::create(err.size()).unwrap();
                b.copy_from(err.data(), 0, err.size());
                co_await cb(b, true);
            }).unwrap();
        });

        router.POST ("/upload", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            ssize_t result = 0;
            manapi::hash::sha256 hash;
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

            b = manapi::crypto::strdec2strhex(b).unwrap();

            std::cout << result << " " << b << "\n";

            co_return resp.text(std::format("{} : {}", result, b)).unwrap();
        });

        router.GET("/mem", "/home/Timur/Downloads/VideoDownloader");
        /**
         * starts I/O jobs.
         * works in this context as long as possible.
         */
        manapi::async::run([router] () mutable -> manapi::future<> {

            manapi::unwrap(co_await router.config ("./config.json"));

            manapi::unwrap(co_await router.start());
        });

        /* bind event loop in the current context */
        bind();
    }).unwrap();

    manapi::clear_tools::curl_library_clear();
    manapi::clear_tools::ev_library_clear();
    manapi::clear_tools::ssl_library_clear();

    return 0;
}
