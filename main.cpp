#include <iostream>

#include <manapihttp/ManapiHttp.hpp>
#include <manapihttp/ManapiInitTools.hpp>
#include <manapihttp/ManapiProcess.hpp>
#include <manapihttp/ManapiString.hpp>
#include <manapihttp/cache/ManapiTL.hpp>
#include <manapihttp/ManapiFetch2.hpp>
#include <manapihttp/ext/pq/AsyncPostgreClient.hpp>

struct data_t {
    std::mutex mx;
};

static std::string zfrontend;
static std::string zconfig;

static manapi::status setup_cors(manapi::net::http::request &req, manapi::net::http::response &resp) MANAPIHTTP_NOEXCEPT {
    try {
        auto const hv = req.http();
        if (hv == manapi::net::http::versions::HTTP_v1_1) {
            resp.header("Access-Control-Allow-Origin", "*").unwrap();
            resp.header("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, PATCH, DELETE").unwrap();
            resp.header("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept, Authorization").unwrap();
            resp.header("Access-Control-Allow-Credentials", "True").unwrap();
        }
        return manapi::status_ok();
    }
    catch (std::exception const &e) {
        manapi_log_error("%s due to %s", "http:setup_cors failed", e.what());
        return manapi::status_internal("http:setup_cors failed");
    }
}

int main() {
    manapi::init_tools::log_name_enable("manapihttp", true);
    manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_LOW);
    zfrontend = manapi::process::get_env("CALCULATORAI_FRONTEND").unwrap();
    zconfig = manapi::process::get_env("CALCULATORAI_CONFIG").unwrap();
    /* creates 2 threads for blocking I/O syscalls */
    manapi::async::context::threadpoolfs(2);
    /* disable several signals */
    manapi::async::context::gbs (manapi::async::context::blockedsignals());
    /* creates 4 additional threads for 4 additional event loops */
    auto ctx = manapi::async::context::create(4).unwrap();
    /* HTTP context for multiple HTTP routers (threadsafe) */
    auto router_ctx = manapi::net::http::server_ctx::create().unwrap();
    /* runs main event loop and 4 additional event loops */
    ctx->run([router_ctx] (std::function<void()> bind) -> void {
        using http = manapi::net::http::server;

        manapi::json app;
        auto router = manapi::net::http::server::create(router_ctx).unwrap();
        auto db = manapi::ext::pq::connection::create().unwrap();

        router.GET("/", zfrontend).unwrap();

        router.GET("/", [] (http::req &req, http::resp &resp) -> manapi::future<> {
            co_return resp.file(manapi::fs::path::join(zfrontend, "index.html")).unwrap();
        }).unwrap();

        router.GET("/+layer", [] (http::req &req, http::resp &resp) -> manapi::future<> {
            manapi::unwrap(setup_cors (req, resp));
            co_return;
        }).unwrap();

        router.POST("/+layer", [] (http::req &req, http::resp &resp) -> manapi::future<> {
            manapi::unwrap(setup_cors (req, resp));
            co_return;
        }).unwrap();

        router.OPTIONS("/+error", [] (http::req &req, http::resp &resp) -> manapi::future<> {
            resp.status(200);
            manapi::unwrap(setup_cors (req, resp));
            co_return;
        }).unwrap();

        router.GET("/api/create", [&app, db] (http::req &req, http::resp &resp) mutable  -> manapi::future<> {
            while (true) {
                auto api = manapi::string::random(78);
                auto tv = 86400000;
                auto content = manapi::json::array();
                content.push_back({
                    {"role", "system"},
                    {"content", app["prompt"]}
                });
                auto result = manapi::unwrap(co_await db.execl("WITH cln AS (DELETE FROM chats WHERE removed_at < NOW())"
                        "INSERT INTO chats (id, chat, created_at, removed_at) VALUES ($1,$2,NOW(),NOW() + $3 * INTERVAL '1 millisecond');",
                        req.cancellation().sub(), api, content.dump(), tv));
                if (!result.affected_rows()) {
                    continue;
                }
                co_return resp.json({{"ok", true}, {"api", api}, {"live_in", tv}}).unwrap();
            }
        }).unwrap();

        router.POST ("/api/[api]/chat", [&app, db] (http::req &req, http::resp &resp) mutable -> manapi::future<> {
            auto api = std::string{req.param("api").unwrap()};
            auto userdata = manapi::unwrap(co_await req.json());
            auto res = manapi::unwrap(co_await db.execl("WITH cln AS (DELETE FROM chats WHERE removed_at < NOW())"
                                                        "UPDATE chats SET version = version + 1 RETURNING version, chat;", req.cancellation().sub()));
            if (res.empty())
                co_return resp.json({{"ok", false}, {"msg", "not found"}}).unwrap();
            auto data = manapi::json::parse(res[0]["chat"].as<std::string>()).unwrap();
            auto version = res[0]["version"].as<int>();
            auto usermsg = manapi::json::object();
            usermsg.insert("role", "user");
            usermsg.insert("content", std::move(userdata["content"]));
            data.push_back(std::move(usermsg));
            // auto b = manapi::string::random(100);
            // for (int i = 0; i < 6; i++)
            //     b = b + b;
            // auto b2 = std::format("{}\n{}\n{}", b, api, b);
            // data->content.push_back({
            //     {"role", "assistant"},
            //     {"content", b},
            //     {"reasoning_details", b}
            // });
            // co_return resp.callback_async([b = std::move(b2), s = 0] (manapi::slice_view buffs, bool &f) mutable -> manapi::future<ssize_t> {
            //     co_await manapi::async::delay{100};
            //     auto size =  std::min<ssize_t>(100, b.size() - s);
            //     buffs.copy_from(b.data() + s, 0,size);
            //     s += size;
            //     if (size==0)
            //         f=true;
            //     co_return size;
            // }).unwrap();
            auto headers = manapi::json::object();
            headers.insert("Authorization", std::format("Bearer {}", app["api-key"].as_string()));
            headers.insert("Content-Type", "application/json");
            auto pfetch = manapi::json::object();
            pfetch.insert("method", "POST");
            pfetch.insert("headers", std::move(headers));
            pfetch.insert("recv_nodelay", true);
            auto body = manapi::json::object();
            auto reasoning = manapi::json::object();
            reasoning.insert("enabled", true);
            body.insert("model", app["model"]);
            body.insert("messages", data);
            body.insert("stream", true);
            body.insert("reasoning", std::move(reasoning));
            auto response = manapi::unwrap(co_await manapi::net::fetch2::fetch ("https://openrouter.ai/api/v1/chat/completions", std::move(pfetch), body.dump()));

            if (!response.ok()) {
                auto text = manapi::unwrap(co_await response.text());
                manapi_log_trace("/api/[api]/chat failed due to %d, %s", response.status(), text.data());
                co_return resp.json({{"ok", false}, {"msg", "response failed"}}).unwrap();
            }

            co_return resp.callback_stream([db, version, data = std::move(data), response, api]
                    (std::move_only_function<manapi::future<ssize_t>(manapi::slice_view buffs, bool)> send) mutable
                        -> manapi::future<> {

                std::string content;
                std::string reasoning;
                manapi::json_builder динислам_строит;
                int ленар_считает = 0;
                int modar_ali = 0;

                manapi::unwrap(co_await response.callback_async([&modar_ali, &динислам_строит, &ленар_считает, &send, &reasoning, &content, &api] (manapi::slice_view sv, bool fin) -> manapi::future<ssize_t> {
                    auto csize = content.size();
                    auto rsize = reasoning.size();
                    try {
                        for (auto boss : sv) {
                            for (size_t i = 0; i < boss.size(); i++) {
                                if (boss[i]=='{' && !ленар_считает) {
                                    ленар_считает=1;
                                }
                                if (ленар_считает) {
                                    динислам_строит << boss[i];

                                    if (boss[i] == '\n') {
                                        if (ленар_считает==2) {
                                            ленар_считает=0;
                                            auto data = динислам_строит.get().unwrap();
                                            динислам_строит.clear();
                                            auto dr = data["choices"][0]["delta"]["reasoning"];
                                            if (dr.is_string())
                                                reasoning += dr.as_string();
                                            auto dc = data["choices"][0]["delta"]["content"];
                                            if (dc.is_string() && !dc.as_string().empty()) {
                                                if (!modar_ali) {
                                                    reasoning += std::format("\n{}\n", api);
                                                    modar_ali = 1;
                                                }
                                                content += dc.as_string();
                                            }
                                        }
                                        else
                                            ленар_считает++;
                                    }
                                    else {
                                        ленар_считает = 1;
                                    }
                                }
                            }
                        }
                    }
                    catch (std::exception const &e) {
                        std::cerr << e.what() << "\n";
                        for (auto boss : sv)
                            std::cerr << boss;
                        std::cerr << "\n";
                        std::rethrow_exception(std::current_exception());
                    }
                    if (reasoning.size() != rsize) {
                        manapi::slice_part_t part{};
                        part.buff.base = reasoning.data() + rsize;
                        part.buff.len = reasoning.size() - rsize;
                        manapi::slice_view b (&part);
                        if (co_await send (b, fin && content.size() == csize) < 0)
                            co_return -1;
                    }
                    if (content.size() != csize) {
                        manapi::slice_part_t part{};
                        part.buff.base = content.data() + csize;
                        part.buff.len = content.size() - csize;
                        manapi::slice_view b (&part);
                        if (co_await send (b, fin) < 0)
                            co_return -1;
                    }
                    else if (fin) {
                        if (co_await send (manapi::slice_view (), fin) < 0)
                            co_return -1;
                    }
                    co_return sv.size();
                }));

                manapi::string::replace(reasoning, api, "");
                auto obj = manapi::json::object();
                obj.insert("role", "assistant");
                obj.insert("content", std::move(content));
                obj.insert("reasoning_details", std::move(reasoning));
                data.push_back(std::move(obj));

                manapi::async::run ([db, api, data = std::move(data), version] () mutable -> manapi::future<> {
                    manapi::unwrap(co_await db.execl("UPDATE chats SET chat = $2 WHERE id = $1 AND version = $3;",
                        manapi::ctokens::timeout(5000), api, data.dump(), version));
                });

            }).unwrap();
        }).unwrap();

        router.POST("/api/+error", [] (http::req &req, http::resp &resp) -> manapi::future<> {
            co_return resp.json({{"ok", false}, {"msg", "something gets wrong"}}).unwrap();
        }).unwrap();

        router.POST ("/api/[api]/alive", [db] (http::req &req, http::resp &resp) mutable  -> manapi::future<> {
            auto api = std::string{req.param("api").unwrap()};
            auto res = manapi::unwrap(co_await db.execl("WITH cln AS (DELETE FROM chats WHERE removed_at < NOW())SELECT id FROM chats WHERE id = $1;", req.cancellation().sub(), api));
            if (res.empty())
                co_return resp.json({{"ok", false}, {"msg", "not found"}}).unwrap();
            co_return resp.json({{"ok", true}}).unwrap();
        }).unwrap();

        router.POST ("/api/[api]/remove", [db](http::req &req, http::resp &resp) mutable  -> manapi::future<> {
            auto api = std::string{req.param("api").unwrap()};
            auto res = manapi::unwrap(co_await db.execl("DELETE FROM chats WHERE id = $1", req.cancellation().sub(), api));
            co_return resp.json({{"ok", true}}).unwrap();
        }).unwrap();

        router.POST ("/api/[api]/history", [db](http::req &req, http::resp &resp) mutable  -> manapi::future<> {
            auto api = std::string{req.param("api").unwrap()};
            auto res = manapi::unwrap(co_await db.execl("WITH cln AS (DELETE FROM chats WHERE removed_at < NOW())SELECT chat FROM chats WHERE id = $1;", req.cancellation().sub(), api));
            if (res.empty())
                co_return resp.json({{"ok", false}, {"msg", "not found"}}).unwrap();
            auto data = manapi::json::parse(res[0]["chat"].as<std::string>()).unwrap();
            manapi::json z = manapi::json::array();
            for (int i = 1; i < data.size(); i++) {
                z.push_back(data[i]);
            }
            co_return resp.json({{"ok", true}, {"data", std::move(z)}}).unwrap();
        }).unwrap();

        manapi::async::run([&app, router, db] () mutable  -> manapi::future<> {
            app = manapi::json::parse(manapi::unwrap(co_await manapi::fs::async_read(zconfig))).unwrap();

            auto &dbapp = app["db"];

            manapi::unwrap(co_await db.connect(dbapp["host"].as_string(), dbapp["port"].as_string(), dbapp["user"].as_string(), dbapp["password"].as_string(), dbapp["db"].as_string()));

            auto pools = manapi::json::array();
            auto pool = manapi::json::object();
            auto version = manapi::json::array();
            version.push_back("1.1");
            pool.insert("address", "10.241.1.226");
            pool.insert("http", std::move(version));
            pool.insert("transport", "tcp");
            pool.insert("port" ,"8888");
            pool.insert("tcp_no_delay", true);
            pool.insert("max_merge_buffer_stack", 0);
            pool.insert("buffer_size", 4096);
            pool.insert("keep_alive", 5);
            pool.insert("max_connections_by_ip", 10000);
            pools.push_back(std::move(pool));

            auto pconfig = manapi::json::object();
            pconfig.insert("pools", std::move(pools));
            pconfig.insert("save_config" , false);
            manapi::unwrap(co_await router.config_object(std::move(pconfig)));
            manapi::unwrap(co_await router.start());
        });

        bind ();
    });

    manapi::clear_tools::curl_library_clear();
    manapi::clear_tools::ev_library_clear();
    manapi::clear_tools::ssl_library_clear();


    return 0;
}
