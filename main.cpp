#include "ManapiHttp.hpp"
#include "services/ManapiFetch2.hpp"
// #include "ext/pq/AsyncPostgreClient.hpp"
#ifdef _WIN32
#   define FOLDER ".\\data\\"
#   define FOLDER2 ".\\data\\"
#else
#define FOLDER2 "/home/Timur/Downloads/anime-main/"
#define FOLDER "/home/Timur/Documents/http2priorities/"
#endif
#include <cstring>

#include "crypto/ManapiAEAD.hpp"
#include "ManapiHash.hpp"
#include "ManapiInitTools.hpp"
#include "ManapiMath.hpp"
#include "ManapiProcess.hpp"
#include "ManapiString.hpp"
#include "async/ManapiAsyncTimer.hpp"
#include "async/ManapiEasyCancellation.hpp"

#include "protobuf/helloworld.grpc.pb.h"
#include "services/ManapiGrpc.hpp"

//#include "extensions/pq/AsyncPostgreClient.hpp"


// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public helloworld::Greeter::CallbackService {
    grpc::ServerUnaryReactor *SayHello(grpc::CallbackServerContext* context, const helloworld::HelloRequest* request,
                    helloworld::HelloReply* reply) override {
        grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
        manapi::async::run ([reactor, reply, request] () -> manapi::future<> {
            auto response = co_await manapi::net::fetch2::fetch (std::format("http://numbersapi.com/{}", manapi::math::random(0, 1000)),
                {
                {"http", "1.1"}
            });
            if (response.ok()) {
                reply->set_message(std::format("Hello, {}! Fact: {}", request->name(), co_await response.text()));
            }
            else {
                reply->set_message(std::format("Hello, {}! Something gets wrong. status: {}", request->name(), response.status()));
            }
            reactor->Finish(grpc::Status::OK);
        });
        return reactor;
    }
};


int main () {
    int threads = 2;
    try { threads = std::stoi(manapi::process::get_env("MANAPIHTTP_THREADS").unwrap()); }
    catch (...) {  }

    manapi::async::context::threadpoolfs(threads);
    manapi::async::context::gbs = manapi::async::context::blockedsignals();

    int loops = 0;
    try { loops = std::stoi(manapi::process::get_env("MANAPIHTTP_LOOPS").unwrap()); }
    catch (...) {  }

    auto ctx = manapi::async::context::create(loops);
    ctx->eventloop()->setup_handle_interrupt();

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    std::atomic<int> a = 0;
    std::atomic<int> thrcnt = 0;

    manapi::net::http::server_ctx server_ctx;
    manapi::net::wgrpc::server_ctx grpc_server_ctx;

    manapi::async::context::run(ctx, loops, [&thrcnt, &a, grpc_server_ctx, server_ctx] (const std::function<void()> &bind) -> void {
        using http = manapi::net::http::server;
        //manapi::ext::pq::connection db;

        /**
         * grpc
         */

        manapi::net::wgrpc::server grpc_server (grpc_server_ctx);
        auto service = std::make_shared<GreeterServiceImpl>();

        manapi::async::run([grpc_server, service] () mutable -> manapi::future<> {
            auto res = co_await grpc_server.config("/home/Timur/Desktop/WorkSpace/ManapiHTTP/cmake-build-debug/grpc.json");

            res.log();

            res = co_await grpc_server.start([&] (grpc::ServerBuilder &builder) -> manapi::error::status {
                builder.RegisterService(service.get());
                return manapi::error::status_ok();
            });

            res.log();
        }, [] (std::exception_ptr err) -> void {
            assert(!err);
        });

        /**
         * http
         */

        std::string const folder = FOLDER;
        manapi::net::http::server router (server_ctx);

        router.GET("/", folder, [] (http::req &req, http::resp &resp)
            -> manapi::future<> {
            resp.compress_enabled(true);
            resp.compress("zstd");
            co_return;
        });

        router.GET("/", [&folder] (http::req &req, http::resp &resp) -> manapi::future<> {
            co_return resp.file(manapi::filesystem::path::join(folder, "index.html"));
        });

        router.GET("/stat", [server_ctx] (http::req &req, http::resp &resp) mutable -> manapi::future<> {
            co_return resp.text(std::to_string(server_ctx.storage().as<manapi::net::http::server_ctx::worker_data_t>()->count.load()));
        });

        router.GET ("/main", [&a] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            co_return resp.text("");
        });

        router.GET ("/free", [&a] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            manapi::init_tools::ev_library_init();
            manapi::async::current()->memory_fabric().clear();
            resp.compress_enabled(false);
            co_return resp.text(std::to_string(a.load()));
        });

        router.GET("/mem", "/home/Timur/Downloads/VideoDownloader");

        manapi::async::run([router] () mutable -> manapi::future<> {
           //co_await db.connect("127.0.0.1", "7879", "development", "rv8FY--PHz_QV<wvT4=n_Ru+cUJE}>KCqmBj9&#M3\\\"Gb.tx", "workflow-main");

            co_await router.config("/home/Timur/Desktop/WorkSpace/ManapiHTTP/cmake-build-debug/config.json");
            co_await router.start();
        });

        bind();
    });
    return 0;
}
