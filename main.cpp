#include <iostream>

#include <ManapiHttp.hpp>
#include <ManapiInitTools.hpp>
#include <ManapiProcess.hpp>
#include <ManapiString.hpp>
#include <cache/ManapiTL.hpp>
#include <ManapiFetch2.hpp>

struct data_t {
    manapi::json content;
    std::mutex mx;
};

static std::string zfrontend;
static std::string zconfig;

static manapi::tl_cache<std::string, std::shared_ptr<data_t>> messages;
static std::mutex mx_messages;

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

        router.GET("/", zfrontend).unwrap();

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

        router.GET("/api/create", [&app] (http::req &req, http::resp &resp) -> manapi::future<> {
            auto api = manapi::string::random(78);
            auto tv = 86400000;
            std::lock_guard<std::mutex> lk (mx_messages);
            auto data = std::make_shared<data_t>();
            data->content = manapi::json::array();
            data->content.push_back({
                {"role", "system"},
                {"content", app["prompt"]}
            });
            messages.put(api, std::move(data), std::chrono::milliseconds (tv + 60000));
            co_return resp.json({{"ok", true}, {"api", api}, {"live_in", tv}}).unwrap();
        }).unwrap();

        router.POST ("/api/[api]/chat", [&app] (http::req &req, http::resp &resp) -> manapi::future<> {
            auto api = std::string{req.param("api").unwrap()};
            auto userdata = manapi::unwrap(co_await req.json());
            std::unique_lock<std::mutex> lk (mx_messages);
            auto data_res = messages.get(api);
            if (!data_res)
                co_return resp.json({{"ok", false}, {"msg", "not found"}}).unwrap();
            auto data = *data_res.unwrap();
            std::unique_lock<std::mutex> lkz (data->mx, std::try_to_lock);
            lk.unlock();
            if (!lkz.owns_lock())
                co_return resp.json({{"ok", false}, {"msg", "already thinking"}}).unwrap();
            data->content.push_back({
                {"role", "user"},
                {"content", std::move(userdata["content"].as_string())}
            });
            auto b = manapi::string::random(100);
            for (int i = 0; i < 6; i++)
                b = b + b;
            auto b2 = std::format("{}\n{}\n{}", b, api, b);
            data->content.push_back({
                {"role", "assistant"},
                {"content", b},
                {"reasoning_details", b}
            });
            co_return resp.callback_async([b = std::move(b2), s = 0] (manapi::slice_view buffs, bool &f) mutable -> manapi::future<ssize_t> {
                co_await manapi::async::delay{1000};
                auto size =  std::min<ssize_t>(100000000, b.size() - s);
                buffs.copy_from(b.data() + s, 0,size);
                s += size;
                if (size==0)
                    f=true;
                co_return size;
            }).unwrap();
            auto response = manapi::unwrap(co_await manapi::net::fetch2::fetch ("https://openrouter.ai/api/v1/chat/completions", {
                {"method", "POST"},
                {"headers", {
                    {"Authorization", std::format("Bearer {}", app["api-key"].as_string())},
                    {"Content-Type", "application/json"}
                }},
                {"recv_nodelay", true}
            }, manapi::json ({
                {"model", app["model"]},
                {"messages", data->content},
                {"stream", true},
                {"reasoning", manapi::json::object({{"enabled", true}})}
            }).dump()));

            if (!response.ok()) {
                auto text = manapi::unwrap(co_await response.text());
                manapi_log_trace("/api/[api]/chat failed due to %d, %s", response.status(), text.data());
                co_return resp.json({{"ok", false}, {"msg", "response failed"}}).unwrap();
            }

            co_return resp.callback_stream([lkz = std::move(lkz), data, response, api]
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
                        co_await send (b, fin && content.size() == csize);
                    }
                    if (content.size() != csize) {
                        manapi::slice_part_t part{};
                        part.buff.base = content.data() + csize;
                        part.buff.len = content.size() - csize;
                        manapi::slice_view b (&part);
                        co_await send (b, fin);
                    }
                    else if (fin) {
                        co_await send (manapi::slice_view (), fin);
                    }
                    co_return sv.size();
                }));

                manapi::string::replace(reasoning, api, "");
                data->content.push_back({
                    {"role", "assistant"},
                    {"content", std::move(content)},
                    {"reasoning_details", std::move(reasoning)}
                });
            }).unwrap();
        }).unwrap();

        router.POST("/api/+error", [] (http::req &req, http::resp &resp) -> manapi::future<> {
            co_return resp.json({{"ok", false}, {"msg", "something gets wrong"}}).unwrap();
        }).unwrap();

        router.POST ("/api/[api]/alive", [] (http::req &req, http::resp &resp) -> manapi::future<> {
            auto api = std::string{req.param("api").unwrap()};
            std::unique_lock<std::mutex> lk (mx_messages);
            auto data_res = messages.get(api);
            if (!data_res)
                co_return resp.json({{"ok", false}, {"msg", "not found"}}).unwrap();
            co_return resp.json({{"ok", true}}).unwrap();
        }).unwrap();

        router.POST ("/api/[api]/remove", [](http::req &req, http::resp &resp) -> manapi::future<> {
            auto api = std::string{req.param("api").unwrap()};
            std::unique_lock<std::mutex> lk (mx_messages);
            messages.remove(api);
            co_return resp.json({{"ok", true}}).unwrap();
        }).unwrap();

        router.POST ("/api/[api]/history", [](http::req &req, http::resp &resp) -> manapi::future<> {
            auto api = std::string{req.param("api").unwrap()};
            std::unique_lock<std::mutex> lk (mx_messages);
            auto data_res = messages.get(api);
            if (!data_res)
                co_return resp.json({{"ok", false}, {"msg", "not found"}}).unwrap();
            auto data = *data_res.unwrap();
            manapi::json z = manapi::json::array();
            for (int i = 1; i < data->content.size(); i++) {
                z.push_back(data->content[i]);
            }
            co_return resp.json({{"ok", true}, {"data", std::move(z)}}).unwrap();
        }).unwrap();

        manapi::async::run([&app, router] () mutable  -> manapi::future<> {
            app = manapi::json::parse(manapi::unwrap(co_await manapi::fs::async_read(zconfig))).unwrap();

            manapi::unwrap(co_await router.config_object({
                {"pools", manapi::json::array({
                    {
                        {"address", "127.0.0.1"},
                        {"http", manapi::json::array({"1.1"})},
                        {"transport", "tcp"},
                        {"partial_data_min_size", 0},
                        {"port", "8888"},
                        {"tcp_no_delay", true},
                        {"max_merge_buffer_stack", 0},
                        {"buffer_size", 4096}
                    }
                })},
                {"save_config", false}
            }));
            manapi::unwrap(co_await router.start());
        });

        bind ();
    });

    manapi::clear_tools::curl_library_clear();
    manapi::clear_tools::ev_library_clear();
    manapi::clear_tools::ssl_library_clear();


    return 0;
}
