#include "./handlers.hpp"

#include "ManapiHash.hpp"
#include "ManapiProcess.hpp"

void init_http_server(manapi::net::http::server &router, std::string const &folder) {
    using http = manapi::net::http::server;

    router.GET("/", folder, [] (http::req &req, http::resp &resp)
        -> manapi::future<> {
        resp.compress_enabled(true);
        resp.compress("zstd");
        co_return;
    });

    router.GET("/http", [] (http::req &req, manapi::net::http::response *resp) -> void {
        std::string http = "";
        auto version = req.http();
        switch (version) {
            case manapi::net::http::versions::HTTP_v0_9: http = "0.9"; break;
            case manapi::net::http::versions::HTTP_v1_0: http = "1.0"; break;
            case manapi::net::http::versions::HTTP_v1_1: http = "1.1"; break;
            case manapi::net::http::versions::HTTP_v2: http = "2"; break;
            case manapi::net::http::versions::HTTP_v3: http = "3"; break;
            default: http = "uknown";
        }
        resp->replacers({
            {"version", std::move(http)},
        });

        resp->file("/home/Timur/Desktop/WorkSpace/ManapiHTTP/examples/http.html");
        resp->finish();
    });

    router.GET("/", [&folder] (http::req &req, http::resp &resp) -> manapi::future<> {
        co_return resp.file(manapi::filesystem::path::join(folder, "index.html")).unwrap();
    });


    router.GET("/noise", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        ssize_t len = 10737418240 / 2;

        resp.header(std::string{manapi::net::http::HEADER.CONTENT_LENGTH}, std::to_string(len));
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
        char *end;
        ssize_t len = std::strtoll(req.param("size").unwrap().data(), &end, 10);

        resp.header(std::string{manapi::net::http::HEADER.CONTENT_LENGTH}, std::to_string(len));
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
        }).unwrap();

        co_return resp.file("/home/Timur/Desktop/WorkSpace/ManapiHTTP/examples/error.html").unwrap();
    });

    router.POST ("/uploadtest", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        co_return resp.callback_stream([&req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
            ssize_t result = 0;
            auto c = std::chrono::steady_clock::now();
            try {
                (co_await req.callback_sync([&c, &result, &cb] (const char *buffer, ssize_t size, bool fin)
                    -> ssize_t {
                    result += size;
                    if (c + std::chrono::seconds (1) <= std::chrono::steady_clock::now()) {
                        auto a = std::format("{}\n", (double)result / 1024 / 1024);
                        result = 0;
                        c = std::chrono::steady_clock::now();
                        std::cout << a << "\n";
                    }
                    return size;
                })).unwrap();
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
        if (req.contains_header(std::string{manapi::net::http::HEADER.CONTENT_LENGTH}))
            resp.header(std::string{manapi::net::http::HEADER.CONTENT_LENGTH},
                std::string{req.header(manapi::net::http::HEADER.CONTENT_LENGTH).unwrap()}).unwrap();

        std::size_t sss = 0;
        co_return resp.callback_stream([&sss, &resp, &req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
            auto fs = manapi::filesystem::fstream::create ("/home/Timur/Downloads/VideoDownloader/ufa.mp4").unwrap();
            auto rhs = co_await fs.open(manapi::ev::FS_O_RDONLY);
            rhs.unwrap();
            (co_await req.callback_async([&sss, cb = std::move(cb), fs] (manapi::slice_view buffs, bool fin) mutable
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
            })).unwrap();
            std::cout << sss << "\n";
        }).unwrap();
    });

    router.POST ("/uploadasynctest", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        co_return resp.callback_stream([&req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
            ssize_t result = 0;
            auto c = std::chrono::steady_clock::now();
            try {
                (co_await req.callback_async([&c, &result, &cb] (manapi::slice_view buffs, bool fin)
                    -> manapi::future<ssize_t> {
                    result += buffs.size();
                    if (c + std::chrono::seconds (1) <= std::chrono::steady_clock::now()) {
                        auto a = std::format("{}\n", (double)result / 1024 / 1024);
                        result = 0;
                        c = std::chrono::steady_clock::now();
                        std::cout << a << " " << buffs.size() << "\n";
                    }
                    co_return buffs.size();
                })).unwrap();
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
        manapi::net::hash::sha256 hash;
        try {
            (co_await req.callback_sync([&result, &hash] (const char *buffer, ssize_t size, bool fin)
                -> ssize_t {
                if (fin) {
                    std::cout << "FINSH\n";
                }
                hash.update(reinterpret_cast<const uint8_t *>(buffer), size);
                result += size;
                return size;
            })).unwrap();
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

    router.POST ("/formdata", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        ssize_t result = 0;
        manapi::net::hash::sha256 hash{};
        try {
            (co_await req.form([&result, &hash] (std::string name) -> manapi::net::formdata_recv::ondata_cb_t {
                return [&hash, &result] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
                    for (auto it = buffs.begin(); it != buffs.end(); it++)
                        hash.update((uint8_t*)it.buffer(), it.size());
                    result += buffs.size();
                    co_return buffs.size();
                };
            })).unwrap();
        }
        catch (std::exception const &e) {
            std::cout << e.what() << "\n";
        }

        std::string b;
        b.resize(36);
        hash.final(reinterpret_cast<uint8_t *>(b.data()));

        b = manapi::crypto::strdec2strhex(b);

        std::cout << result << " " << b << "\n";

        co_return resp.text(std::format("{} : {}", result, b)).unwrap();
    });

    router.POST ("/formdatatest", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        ssize_t result = 0;
        auto c = std::chrono::steady_clock::now();
        try {
            (co_await req.form([&result, &c] (std::string name) -> manapi::net::formdata_recv::ondata_cb_t {
                return [&] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
                    result += buffs.size();
                    if (c + std::chrono::seconds (1) <= std::chrono::steady_clock::now()) {
                        auto a = std::format("{}\n", (double)result / 1024 / 1024);
                        result = 0;
                        c = std::chrono::steady_clock::now();
                        std::cout << a << "\n";
                    }
                    co_return buffs.size();
                };
            })).unwrap();
        }
        catch (std::exception const &e) {
            std::cout << e.what() << "\n";
        }



        auto bb = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - c);
        std::string err = std::format("{}\n", ((double)result / 1024 / 1024) / ((double)bb.count()/1000));

        co_return resp.text(err).unwrap();
    });

    router.GET ("/chunked", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        try {
            auto cancellation = manapi::async::cancellation_action::unit(req.cancellation());
            cancellation.timeout(5000);
            cancellation.ask_cancel_callback();
            auto file = manapi::filesystem::fstream::create ("/home/Timur/Downloads/VideoDownloader/ufa.mp4",
                cancellation).unwrap();
            co_await file.open (manapi::ev::FS_O_RDONLY|manapi::ev::FS_O_NONBLOCK);
            if (!file.is_open()) {
                co_return resp.text("failed to open the file").unwrap();
            }

            auto fetch = (co_await manapi::net::fetch2::fetch("https://localhost:8885/upload", {
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
            }, [file] (manapi::slice_view buffs, bool &fin) mutable -> manapi::future<ssize_t> {
                auto const res = co_await file.fread(buffs);
                fin = file.eof();
                co_return res;
            }, manapi::async::cancellation_action::unit(cancellation))).unwrap();

            co_await file.close();

            if (!fetch.ok()) {
                co_return resp.text(std::format("status : {}", fetch.status())).unwrap();
            }
            co_return resp.text((co_await fetch.text()).unwrap()).unwrap();
        }
        catch (std::exception const &e) {
            co_return resp.text(e.what()).unwrap();
        }
    });

    router.GET("/fetch_async_sha256", [] (http::req &req, http::resp &resp)
        -> manapi::future<> {
        auto f = co_await manapi::net::fetch2::fetch("https://127.0.0.1:8885/video", {
            {"method", "GET"},
            {"http", "2"},
            {"verify_peer", false},
            {"verify_host", false}
        }, req.cancellation().sub());
        if (!f.ok()) {
            std::string s = "error: ";
            s += f.message();
            co_return resp.text(s).unwrap();
        }
        manapi::net::hash::sha256 sha256{};
        ssize_t res = 0;
        auto response = f.unwrap();

        (co_await response.callback_async([&] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
            res += buffs.size();
            for (auto it = buffs.begin(); it != buffs.end(); it++)
                sha256.update((uint8_t *)it.buffer(), it.size());
            co_return buffs.size();
        })).unwrap();

        std::string b;
        b.resize(36);
        sha256.final(reinterpret_cast<uint8_t *>(b.data()));

        b = manapi::crypto::strdec2strhex(b).unwrap();
        co_return resp.text(std::format("{} {}", res, b)).unwrap();
    });

    router.GET("/fetch_async_test", [] (http::req &req, http::resp &resp)
        -> manapi::future<> {
        auto f = co_await manapi::net::fetch2::fetch("http://127.0.0.1:8889/noise", {
            {"method", "GET"}
        },
            manapi::async::cancellation_action::unit(req.cancellation()));
        if (!f.ok()) {
            std::string s = "error: ";
            s += f.message();
            co_return resp.text(s).unwrap();
        }
        auto response = f.unwrap();
        ssize_t res = 0;
        co_await response.callback_async([&] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
            res += buffs.size();
            co_return buffs.size();
        });
        co_return resp.text(std::to_string(res)).unwrap();
    });

    router.GET ("/ai", [] (http::req &req, http::resp &resp)
            -> manapi::future<> {
            if (!req.contains_get_param("text")) {
                co_return resp.text("GET param 'text' doesn't exists").unwrap();
            }

            std::string ip = "https://openrouter.ai/api/v1/chat/completions";
            int timeout = 64000;
            if (req.contains_get_param("timeout")) {
                try {
                    auto s = req.get_extract("timeout").unwrap();
                    timeout = std::stoi(s.second);
                }
                catch (...) {

                }
            }
            if (req.contains_get_param("ip"))
                ip = req.get("ip").unwrap();

            auto text = req.get("text").unwrap();

            auto token = manapi::process::get_env("MANAPIHTTP_AI").unwrap();

            auto cancellation = req.cancellation().sub();
        cancellation.timeout(timeout);

            auto response = co_await manapi::net::fetch2::fetch(ip, {
                {"method", "POST"},
                {"verify_peer", false},
                {"alpn", true},
                {"verbose", true},
                {"headers", {
                    {"Content-Type", "application/json"},
                    {"Authorization", std::format("Bearer {}", token)}
                }}
            }, manapi::json({
                {"model", "tngtech/deepseek-r1t2-chimera:free"},
                {"messages", manapi::json::array({
                    {
                        {"role", "user"},
                        {"content", std::move(text)}
                    }
                })}
            }).dump(), cancellation);

            if (!response.ok()) {
                co_return resp.text(std::format("fetch failed. Http:", response.message())).unwrap();
            }

            resp.callback_stream([response = response.unwrap()] (manapi::net::http::response::resp_stream_cb cb) mutable -> manapi::future<> {
                (co_await response.callback_async([&] (manapi::slice_view buffs, bool fin) mutable -> manapi::future<ssize_t> {
                    co_return co_await cb (buffs, fin);
                })).unwrap();
            }).unwrap();
        });
}
