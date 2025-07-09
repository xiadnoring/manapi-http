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
        std::string prefix("Hello ");
        reply->set_message(prefix + request->name());

        grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
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

    std::atomic<int> a = 0;
    std::atomic<int> thrcnt = 0;
    manapi::net::http::server_ctx server_ctx;
    auto aa = std::make_shared<manapi::manapi_grpc_event_engine_wrapper>();

    manapi::async::context::run(ctx, loops, [&aa, &thrcnt, &a, server_ctx] (const std::function<void()> &bind) -> void {
        using http = manapi::net::http::server;
        //manapi::ext::pq::connection db;
        manapi::net::http::server router (server_ctx);

        std::string folder;

        grpc_event_engine::experimental::SetDefaultEventEngine(aa);

        std::string server_address = absl::StrFormat("0.0.0.0:%d", 8080);
        GreeterServiceImpl service;
        grpc::EnableDefaultHealthCheckService(true);
        grpc::reflection::InitProtoReflectionServerBuilderPlugin();
        grpc::ServerBuilder builder;
        // Listen on the given address without any authentication mechanism.

        // Register "service" as the instance through which we'll communicate with
        // clients. In this case it corresponds to an *synchronous* service.
        builder.RegisterService(&service);
        // Finally assemble the server.
        std::unique_ptr<grpc::Server> server;

        // manapi::async::run([&] () -> manapi::future<> {
        //     grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp;
        //     pkcp.cert_chain = co_await manapi::filesystem::async_read("/home/Timur/Documents/ssl/quic/cert.crt");
        //     pkcp.private_key = co_await manapi::filesystem::async_read("/home/Timur/Documents/ssl/quic/cert.key");
        //     grpc::SslServerCredentialsOptions ssl_opts;
        //     ssl_opts.pem_root_certs="";
        //     ssl_opts.pem_key_cert_pairs.push_back(pkcp);
        //     std::shared_ptr<grpc::ServerCredentials> creds;
        //     creds = grpc::SslServerCredentials(ssl_opts);
        //     builder.AddListeningPort(server_address, creds);
        //     server = builder.BuildAndStart();
        //     std::cout << "Server listening on " << server_address << std::endl;
        // });


        if (thrcnt.fetch_add(1) % 2 == 0)
            folder = FOLDER;
        else
            folder = FOLDER;

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
