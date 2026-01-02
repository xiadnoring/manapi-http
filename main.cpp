#include <iostream>

#include <ManapiHttp.hpp>
#include <ManapiInitTools.hpp>

#include "cache/ManapiLRU.hpp"
#include "ext/pq/AsyncPostgreClient.hpp"
#include "ext/pq/AsyncPostgrePool.hpp"
#define FOLDER "/home/Timur/Downloads/anime-main/"

int main() {
    manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_HARD);

    manapi::async::context::threadpoolfs(4);
    manapi::async::context::gbs(manapi::async::context::blockedsignals());

    auto ctx = manapi::async::context::create(0).unwrap();

    auto server_ctx = manapi::net::http::server_ctx::create().unwrap();
    ctx->run(0, [server_ctx] (auto cb) -> void {
        using http = manapi::net::http::server;

        manapi::lru_cache<std::string, std::string> caching (10);

        auto route = manapi::net::http::server::create(server_ctx).unwrap();
        auto db = manapi::ext::pq::db::create().unwrap();

        route.GET ("/", [db, &caching] (http::req &req, http::resp &resp) mutable
                -> manapi::future<> {
            if (req.contains_get_param("k") && req.contains_get_param("v")) {
                auto key = req.get("k").unwrap();
                auto val = req.get("v").unwrap();
                caching.put(std::string{key}, std::string(val), val.size());
                co_return resp.text("OK!").unwrap();
            }
            else if (req.contains_get_param("k")) {
                auto key = req.get("k").unwrap();
                auto res = caching.get(std::string{key});
                co_return resp.text(*res.unwrap()).unwrap();
            }
            else {
                co_return resp.text("k param").unwrap();
            }
        }).unwrap();

        manapi::async::run ([route, db] () mutable -> manapi::future<> {
            auto master = manapi::ext::pq::pool::create().unwrap();
            manapi::unwrap(co_await master->connect(2, "127.0.0.1", "7879", "development", "", "store"));
            db->set_master(std::move(master)).unwrap();

            auto slave = manapi::ext::pq::pool::create().unwrap();
            manapi::unwrap(co_await slave->connect(2, "127.0.0.1", "7879", "development", "", "store"));
            db->add_slave(std::move(slave)).unwrap();

            manapi::unwrap(co_await route.config(manapi::filesystem::path::join(".", "config.json")));
            manapi::unwrap(co_await route.start ());
        });

        cb();
    }).unwrap();

    manapi::clear_tools::curl_library_clear();
    manapi::clear_tools::ev_library_clear();
    manapi::clear_tools::ssl_library_clear();

    return 0;
}