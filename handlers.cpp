#include "./handlers.hpp"

#include "ManapiProcess.hpp"
#include "hash/ManapiSHA256.hpp"
#include "std/ManapiRef.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wsign-compare"

void init_http_server(std::shared_ptr<manapi::net::http::server> router, std::string const &folder) {
    using http = manapi::net::http::server;

    router->GET("/", folder, [] (http::req &req, http::resp &resp)
        -> manapi::future<> {
        auto z = manapi::math::random (0, 2);
        resp.compress(z == 0 ? "zstd" : (z == 1 ? "br" : "gzip"));
        resp.compress_enabled(true);
        co_return;
    });

    router->GET("/f", "/home/Timur/Downloads/VideoDownloader");

    router->GET("/http", [] (http::req &req, http::uresp resp) -> void {
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

        resp->file("/home/timur/Рабочий стол/WorkSpace/ManapiHTTP/examples/http.html");
        resp.finish();
    });


    router->GET("/json", [] (http::req &req, http::uresp resp) -> void {
        return resp->json({{"message", "Hello, World!"}}).unwrap();
    });

    router->POST("/json", [] (http::req &req, http::resp &resp) -> manapi::future<> {
        manapi::json_mask mask = {
            {"hello", R"({string(>=5 <10)})"}
        };

        auto data = co_await req.json(&mask);

        if (!data) {
            co_return resp.json({{"error", true}, {"msg", data.message()},
                {"pos", data.pos()}, {"path", data.path()}, {"data", data.additional_data()}}).unwrap();
        }

        co_return resp.json(data.unwrap()).unwrap();
    });

    router->GET("/", [&folder] (http::req &req, http::resp &resp) -> manapi::future<> {
        co_return resp.file(manapi::fs::path::join(folder, "index.html")).unwrap();
    });


    router->GET("/noise", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        std::size_t len = 10737418240 / 2;

        resp.header(std::string{manapi::net::http::H_CONTENT_LENGTH}, std::to_string(len));
            co_return resp.callback_sync([current = static_cast<std::size_t>(0), len] (char *buffer, std::size_t size, bool &flg) mutable
                    -> ssize_t {
                size = std::min(size, len - current);
                memset(buffer, '\0', size);
                len -= size;
                if (!len)
                    flg = true;
                return (ssize_t)size;
            }).unwrap();
    });

    router->GET("/noise/[size]", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        char *end;
        std::size_t len = (std::size_t)std::strtoll(req.param("size").unwrap().data(), &end, 10);

        resp.header(std::string{manapi::net::http::H_CONTENT_LENGTH}, std::to_string(len));
        co_return resp.callback_sync([current = (std::size_t)0, len] (char *buffer, std::size_t size, bool &flg) mutable
                    -> ssize_t {
                size = std::min(size, len - current);
                memset(buffer, '\0', size);
                len -= size;
                if (!len)
                    flg = true;
                return (ssize_t)size;
            }).unwrap();
    });

    router->GET("/+error", [] (manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
        resp.replacers({
            {"status_code", std::to_string(resp.status_code())},
            {"status_message", std::string{resp.status_message()}}
        }).unwrap();

        co_return resp.file("/home/timur/Рабочий стол/WorkSpace/ManapiHTTP/examples/error.html").unwrap();
    });

    router->GET("/err/+layer", [] (manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
        resp.header("x-test-header", "APPROVED").unwrap();
        co_return;
    });

    router->GET("/err/+error", [] (manapi::net::http::request &req, manapi::net::http::response &resp) -> manapi::future<> {
        resp.replacers({
               {"status_code", std::to_string(resp.status_code())},
               {"status_message", std::string{resp.status_message()}}
       }).unwrap();

        co_return resp.file("/home/timur/Рабочий стол/WorkSpace/ManapiHTTP/examples/error.html").unwrap();
    });


    router->POST ("/uploadtest", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        co_return resp.callback_stream([&req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
            ssize_t result = 0;
            auto c = std::chrono::steady_clock::now();
            try {
                (co_await req.callback_sync([&c, &result, &cb] (const char *buffer, std::size_t size, bool fin)
                    -> ssize_t {
                    result += (ssize_t)(size);
                    if (c + std::chrono::seconds (1) <= std::chrono::steady_clock::now()) {
                        auto a = std::format("{}\n", (double)result / 1024 / 1024);
                        result = 0;
                        c = std::chrono::steady_clock::now();
                        std::cout << a << "\n";
                    }
                    return static_cast<ssize_t>(size);
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


    router->GET("/download", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        co_return resp.file("/home/Timur/Desktop/WorkSpace/oneworld/test.ISO").unwrap();
    });

    router->POST ("/echo", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        if (req.contains_header(std::string{manapi::net::http::H_CONTENT_LENGTH}))
            resp.header(std::string{manapi::net::http::H_CONTENT_LENGTH},
                std::string{req.header(manapi::net::http::H_CONTENT_LENGTH).unwrap()}).unwrap();

        std::size_t sss = 0;
        co_return resp.callback_stream([&sss, &resp, &req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
            auto fs = manapi::fs::fstream::create ("/home/Timur/Downloads/VideoDownloader/ufa.mp4").unwrap();
            auto rhs = co_await fs->open(manapi::ev::FS_O_RDONLY);
            rhs.unwrap();
            (co_await req.callback_async([&sss, cb = std::move(cb), fs] (manapi::slice_view buffs, bool fin) mutable
                -> manapi::future<ssize_t> {
                auto buffs2 = manapi::async::current()->memory_fabric().slice(buffs.size()).unwrap();
                buffs2.resize(buffs.size());
                assert(buffs.size() == buffs2.size());
                auto res = co_await fs->fread(buffs2);
                assert(res == buffs.size());
                auto cmp = buffs.cmp(buffs2);
                if (cmp) {
                    perror("buffs.cmp failed!!!");
                    co_return -1;
                }

                //sum += size;
                //std::cout << sum << " " << size << " " << fin << "\n";
                auto result = co_await cb (buffs, fin);
                sss+=(std::size_t)result;
                fs->seekg((ssize_t)fs->tellg() - (ssize_t)buffs2.size() + (ssize_t)result);
                co_return result;
            })).unwrap();
            std::cout << sss << "\n";
        }).unwrap();
    });

    router->POST ("/uploadasynctest", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        co_return resp.callback_stream([&req] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
            std::size_t result = 0;
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
                    co_return (ssize_t)buffs.size();
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

    router->POST ("/upload", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        ssize_t result = 0;
        manapi::hash::sha256 hash;
        try {
            (co_await req.callback_sync([&result, &hash] (const char *buffer, ssize_t size, bool fin)
                -> ssize_t {
                if (fin) {
                    std::cout << "FINSH\n";
                }
                hash.update(reinterpret_cast<const uint8_t *>(buffer), (std::size_t)size);
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

    router->GET("/mem", "/home/timur/Загрузки/VideoDownloader");

    router->POST ("/formdata", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        ssize_t result = 0;
        manapi::hash::sha256 hash{};
        try {
            (co_await req.form([&result, &hash] (std::string name) -> manapi::net::formdata_recv::ondata_cb_t {
                return [&hash, &result] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
                    for (auto it = buffs.begin(); it != buffs.end(); it++)
                        hash.update((uint8_t*)it.buffer(), it.size());
                    result += (ssize_t)buffs.size();
                    co_return(ssize_t) buffs.size();
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

    router->POST ("/formdatatest", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
        -> manapi::future<> {
        ssize_t result = 0;
        auto c = std::chrono::steady_clock::now();
        try {
            (co_await req.form([&result, &c] (std::string name) -> manapi::net::formdata_recv::ondata_cb_t {
                return [&] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
                    result += (ssize_t)buffs.size();
                    if (c + std::chrono::seconds (1) <= std::chrono::steady_clock::now()) {
                        auto a = std::format("{}\n", (double)result / 1024 / 1024);
                        result = 0;
                        c = std::chrono::steady_clock::now();
                        std::cout << a << "\n";
                    }
                    co_return (ssize_t)buffs.size();
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

//    router->GET ("/chunked", [] (manapi::net::http::request &req, manapi::net::http::response &resp)
//        -> manapi::future<> {
//        try {
//            auto cancellation = manapi::ctoken::unit(req.cancellation());
//            cancellation.timeout(5000);
//            cancellation.ask_cancel_callback();
//            auto file = manapi::fs::fstream::create ("/home/Timur/Downloads/VideoDownloader/ufa.mp4",
//                cancellation).unwrap();
//            co_await file->open (manapi::ev::FS_O_RDONLY|manapi::ev::FS_O_NONBLOCK);
//            if (!file->is_open()) {
//                co_return resp.text("failed to open the file").unwrap();
//            }
//
//            auto fetch = (co_await manapi::net::fetch2::fetch("https://localhost:8885/upload", {
//                {"http", "2"},
//                {"verify_peer", false},
//                {"verbose", false},
//                {"alpn", false},
//                {"method", "POST"},
//                {"timeout", 5},
//                {"headers", {
//                    //{"transfer-encoding", "chunked"}
//                    {"content-length", "298512394"}
//                }}
//            }, [file] (manapi::slice_view buffs, bool &fin) mutable -> manapi::future<ssize_t> {
//                auto const res = co_await file->fread(buffs);
//                fin = file->eof();
//                co_return res;
//            }, manapi::ctoken::unit(cancellation))).unwrap();
//
//            file->close();
//
//            if (!fetch->ok()) {
//                co_return resp.text(std::format("status : {}", fetch->status())).unwrap();
//            }
//            co_return resp.text((co_await fetch->text()).unwrap()).unwrap();
//        }
//        catch (std::exception const &e) {
//            co_return resp.text(e.what()).unwrap();
//        }
//    });
//
//    router->GET("/fetch_sync_sha256", [] (http::req &req, http::resp &resp)
//        -> manapi::future<> {
//        auto f = co_await manapi::net::fetch2::fetch("https://127.0.0.1:8885/mem/ufa.mp4", {
//            {"method", "GET"},
//            {"http", "2"},
//            {"verify_peer", false},
//            {"verify_host", false}
//        }, req.cancellation().sub());
//        if (!f.ok()) {
//            std::string s = "error: ";
//            s += f.message();
//            co_return resp.text(s).unwrap();
//        }
//        manapi::hash::sha256 sha256{};
//        ssize_t res = 0;
//        auto response = f.unwrap();
//
//        (co_await response->callback_sync([&] (char *buff, ssize_t size) -> ssize_t {
//            res += size;
//            sha256.update((uint8_t *)buff, (std::size_t)size);
//            return size;
//        })).unwrap();
//
//        std::string b;
//        b.resize(36);
//        sha256.final(reinterpret_cast<uint8_t *>(b.data()));
//
//        b = manapi::crypto::strdec2strhex(b).unwrap();
//        co_return resp.text(std::format("{} {}", res, b)).unwrap();
//    });
//
//    router->GET("/fetch_async_sha256", [] (http::req &req, http::resp &resp)
//        -> manapi::future<> {
//        auto f = co_await manapi::net::fetch2::fetch("https://127.0.0.1:8885/mem/ufa.mp4", {
//            {"method", "GET"},
//            {"http", "2"},
//            {"verify_peer", false},
//            {"verify_host", false}
//        }, req.cancellation().sub());
//        if (!f.ok()) {
//            std::string s = "error: ";
//            s += f.message();
//            co_return resp.text(s).unwrap();
//        }
//        manapi::hash::sha256 sha256{};
//        ssize_t res = 0;
//        auto response = f.unwrap();
//
//        (co_await response->callback_async([&] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
//            res += (ssize_t)buffs.size();
//            for (auto it = buffs.begin(); it != buffs.end(); it++)
//                sha256.update((uint8_t *)it.buffer(), it.size());
//            co_return (ssize_t)buffs.size();
//        })).unwrap();
//
//        std::string b;
//        b.resize(36);
//        sha256.final(reinterpret_cast<uint8_t *>(b.data()));
//
//        b = manapi::crypto::strdec2strhex(b).unwrap();
//        co_return resp.text(std::format("{} {}", res, b)).unwrap();
//    });
//
//    router->GET("/fetch_async_test", [] (http::req &req, http::resp &resp)
//        -> manapi::future<> {
//        auto f = co_await manapi::net::fetch2::fetch("http://127.0.0.1:8889/noise", {
//            {"method", "GET"}
//        },
//            manapi::ctoken::unit(req.cancellation()));
//        if (!f.ok()) {
//            std::string s = "error: ";
//            s += f.message();
//            co_return resp.text(s).unwrap();
//        }
//        auto response = f.unwrap();
//        ssize_t res = 0;
//        co_await response->callback_async([&] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
//            res += (ssize_t)buffs.size();
//            co_return (ssize_t)buffs.size();
//        });
//        co_return resp.text(std::to_string(res)).unwrap();
//    });
//
//    router->GET ("/ai", [] (http::req &req, http::resp &resp)
//            -> manapi::future<> {
//            if (!req.contains_get_param("text")) {
//                co_return resp.text("GET param 'text' doesn't exist").unwrap();
//            }
//
//            std::string ip = "https://openrouter.ai/api/v1/chat/completions";
//            int timeout = 64000;
//            if (req.contains_get_param("timeout")) {
//                try {
//                    auto s = req.get_extract("timeout").unwrap();
//                    timeout = std::stoi(s.second);
//                }
//                catch (...) {
//
//                }
//            }
//            if (req.contains_get_param("ip"))
//                ip = req.get("ip").unwrap();
//
//            auto text = req.get("text").unwrap();
//
//            auto token = manapi::process::get_env("MANAPIHTTP_AI").unwrap();
//
//            auto cancellation = req.cancellation().sub();
//        cancellation.timeout((std::size_t)timeout).unwrap();
//
//            auto response = (co_await manapi::net::fetch2::fetch(ip, {
//                {"method", "POST"},
//                {"verify_peer", false},
//                {"alpn", true},
//                {"verbose", true},
//                {"headers", {
//                    {"Content-Type", "application/json"},
//                    {"Authorization", std::format("Bearer {}", token)}
//                }}
//            }, manapi::json({
//                {"model", "openai/gpt-oss-20b:free"},
//                {"messages", manapi::json::array({
//                    {
//                        {"role", "user"},
//                        {"content", std::move(text)}
//                    }
//                })}
//            }).dump(), cancellation)).unwrap();
//
//            if (!response->ok()) {
//                co_return resp.text(std::format("fetch failed. Http:", response->status())).unwrap();
//            }
//
//            auto ans = (co_await response->json()).unwrap();
//            co_return resp.text(ans.dump(4)).unwrap();
//        });
}
#pragma GCC diagnostic pop