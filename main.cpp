#include <iostream>
#include <ManapiGrpc.hpp>

#include <ManapiHttp.hpp>
#include <ManapiInitTools.hpp>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include "ManapiFetch2.hpp"
#include "ext/pq/AsyncPostgreClient.hpp"
#include "ext/pq/AsyncPostgrePool.hpp"

#include "protobuf/helloworld.grpc.pb.h"

#define FOLDER "/home/Timur/Downloads/anime-main/"


// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public helloworld::Greeter::CallbackService {
    grpc::ServerUnaryReactor *SayHello(grpc::CallbackServerContext* context, const helloworld::HelloRequest* request,
                    helloworld::HelloReply* reply) override {
        grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
        manapi::async::run ([reactor, reply, request] () -> manapi::future<> {
            try {
                reply->set_message(std::format("Hello, {}! Fact: {}", request->name(), "im happy"));
            }
            catch (std::exception const &e) {
                reply->set_message(std::format("Hello, {}! Something gets wrong: {}", request->name(), e.what()));
            }
            reactor->Finish(grpc::Status::OK);
        });
        return reactor;
    }
};

int main() {
    manapi::init_tools::log_name_enable("manapihttp::fs", true);
    manapi::init_tools::log_name_enable("manapihttp", true);
    manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_HARD);

    manapi::async::context::threadpoolfs(4);
    manapi::async::context::gbs(manapi::async::context::blockedsignals());

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    auto ctx = manapi::async::context::create(2).unwrap();
    auto grpc_server_ctx = manapi::net::wgrpc::server_ctx::create().unwrap();

    auto server_ctx = manapi::net::http::server_ctx::create().unwrap();
    ctx->run(2, [grpc_server_ctx, server_ctx] (auto cb) -> void {
        using http = manapi::net::http::server;

        std::shared_ptr<GreeterServiceImpl> service;
        manapi::net::wgrpc::server grpc_server;

        auto route = manapi::net::http::server::create(server_ctx).unwrap();
        auto db = manapi::ext::pq::db::create().unwrap();

        service = std::make_shared<GreeterServiceImpl>();

        grpc_server = manapi::net::wgrpc::server::create (grpc_server_ctx).unwrap();
        // manapi::async::run([grpc_server, service] () mutable -> manapi::future<> {
        //     auto res = co_await grpc_server.config_object({
        //         {"ssl", {
        //             {"cert", "/home/Timur/Documents/ssl/quic/cert.crt"},
        //             {"key", "/home/Timur/Documents/ssl/quic/cert.key"},
        //             {"verify_peer", false}
        //         }},
        //         {"address", "localhost"},
        //         {"port", "8080"}
        //     });
        //
        //     res.log();
        //     res.unwrap();
        //
        //     // res = co_await grpc_server.start([&] (grpc::ServerBuilder &builder) -> manapi::status {
        //     //     builder.RegisterService(service.get());
        //     //     return manapi::status_ok();
        //     // });
        //     //
        //     // res.log();
        //     // res.unwrap();
        // });

        route.GET ("/", [db] (http::req &req, http::resp &resp) mutable
                -> manapi::future<> {

            manapi::ext::pq::result res = manapi::unwrap(co_await db->exec(manapi::ext::pq::kSlave, "SELECT * FROM test;"));
            std::string content;
            for (auto row : res) {
                content += row["text"].as<std::string_view>();
                content += " | ";
            }

            resp.replacers({
                {"data", std::move(content)}
            }).unwrap();

            co_return resp.file("../test.html").unwrap();
        }).unwrap();


        route.GET ("/", "../utils").unwrap();

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

            manapi::unwrap(co_await route.config(manapi::fs::path::join(".", "config.json")));
            manapi::unwrap(co_await route.start ());
        });

        cb();
    }).unwrap();

    manapi::clear_tools::curl_library_clear();
    manapi::clear_tools::ev_library_clear();
    manapi::clear_tools::ssl_library_clear();

    return 0;
}