#include <iostream>

#include <ManapiHttp.hpp>
#include <ManapiInitTools.hpp>
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

        auto route = manapi::net::http::server::create(server_ctx).unwrap();
        auto db = manapi::ext::pq::db::create().unwrap();

        route.GET ("/", [db] (http::req &req, http::resp &resp) mutable
                -> manapi::future<> {

            manapi::ext::pq::result res = manapi::unwrap(co_await db->exec(manapi::ext::pq::kSlave, "SELECT * FROM test;"));
            std::string content;
            for (auto row : res) {
                content += row["text"].as<std::string>() + "\n";
            }

            resp.replacers({
                {"data", std::move(content)}
            }).unwrap();

            co_return resp.file("../test.html").unwrap();
        }).unwrap();

        route.POST ("/", [db] (http::req &req, http::resp &resp) mutable -> manapi::future<> {
            std::string name;
            manapi::unwrap(co_await req.form([&name] (std::string key) {
                if (key != "text") {
                    throw std::runtime_error ("Invalid param");
                }
                return manapi::net::formdata_recv::save_string(&name, 500);
            }));
            manapi::ext::pq::result res = manapi::unwrap(co_await db->execl(manapi::ext::pq::kMaster, "INSERT INTO test (text) VALUES ($1);",
                manapi::ctokens::timeout(500), name));
            co_return resp.json({{"code", 0}, {"msg", "OK"}}).unwrap();
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