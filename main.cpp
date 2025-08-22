#include "ManapiHttp.hpp"
#include "include/ManapiFetch2.hpp"
// #include "ext/pq/AsyncPostgreClient.hpp"
#ifdef _WIN32
#   define FOLDER ".\\data\\"
#   define FOLDER2 ".\\data\\"
#else
#define FOLDER "/home/Timur/Downloads/anime-main/"
#define FOLDER2 "/home/Timur/Documents/http2priorities/"
#endif
#include <cstring>

#include "handlers.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "hash/ManapiSHA256.hpp"
#include "ManapiInitTools.hpp"
#include "ManapiMath.hpp"
#include "ManapiProcess.hpp"
#include "ManapiString.hpp"
#include "std/ManapiAsyncTimer.hpp"
#include "std/ManapiEasyCancellation.hpp"
#include "ext/ManapiMustache.hpp"

// #include "protobuf/helloworld.grpc.pb.h"
#include "ManapiGrpc.hpp"

#include "include/std/ManapiFunction.hpp"
// //
// #include "ext/pq/AsyncPostgreClient.hpp"
//
//
// // Logic and data behind the server's behavior.
// class GreeterServiceImpl final : public helloworld::Greeter::CallbackService {
//     grpc::ServerUnaryReactor *SayHello(grpc::CallbackServerContext* context, const helloworld::HelloRequest* request,
//                     helloworld::HelloReply* reply) override {
//         grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
//         manapi::async::run ([reactor, reply, request] () -> manapi::future<> {
//             try {
//                 auto status = co_await manapi::net::fetch2::fetch ("https://localhost:8887/stat",{
//                     {"http", "1.1"},
//                     {"verify_peer", false},
//                     {"verify_host", false}
//                 }, manapi::async::timeout_cancellation(2000));
//
//                 if (status.ok()) {
//                     auto response = status.unwrap();
//                     if (response.ok()) {
//                         reply->set_message(std::format("Hello, {}! Fact: {}", request->name(), (co_await response.text()).unwrap()));
//                     }
//                     else {
//                         reply->set_message(std::format("Hello, {}! Something gets wrong. status: {}", request->name(), response.status()));
//                     }
//                 }
//                 else {
//                     reply->set_message(std::format("Hello, {}! Something gets wrong. status: {}", request->name(), status.message()));
//                 }
//             }
//             catch (std::exception const &e) {
//                 reply->set_message(std::format("Hello, {}! Something gets wrong: {}", request->name(), e.what()));
//             }
//             reactor->Finish(grpc::Status::OK);
//         });
//         return reactor;
//     }
// };
//
// class GreeterClient {
// public:
//     GreeterClient(std::shared_ptr<grpc::Channel> channel)
//         : stub_(helloworld::Greeter::NewStub(channel)) {}
//
//     // Assembles the client's payload, sends it and presents the response back
//     // from the server.
//     manapi::future<manapi::error::status_or<std::string>> SayHello(const std::string& user) {
//         using promise = manapi::async::promise_sync<manapi::error::status_or<std::string>>;
//         // Data we are sending to the server.
//         helloworld::HelloRequest request;
//         request.set_name(user);
//
//         // Container for the data we expect from the server.
//         helloworld::HelloReply reply;
//
//         // Context for the client. It could be used to convey extra information to
//         // the server and/or tweak certain RPC behaviors.
//         grpc::ClientContext context;
//
//         co_return co_await promise ([&] (promise::resolve_t resolve, promise::reject_t) -> void {
//             try {
//                 this->stub_->async()->SayHello(&context, &request, &reply, [&, resolve = std::move(resolve)] (grpc::Status status) {
//                     if (status.ok()) {
//                         resolve(reply.message());
//                         return;
//                     }
//
//                     auto msg = status.error_message();
//                     manapi_log_debug("grpc client failed due to %s", msg.data());
//                     resolve(manapi::error::status_internal("grpc client: something gets wrong"));
//                 });
//             }
//             catch (std::exception const &e) {
//                 manapi_log_error(e.what());
//                 resolve(manapi::error::status_internal("sayhello failed"));
//             }
//         });
//     }
//
// private:
//     std::unique_ptr<helloworld::Greeter::Stub> stub_;
// };


int main () {
    try {
        manapi::init_tools::log_trace_init((manapi::debug::trace_level)std::stoi(manapi::process::get_env("MANAPIHTTP_LOGTRACE").unwrap()));
    }
    catch (...) {
        manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_LOW);
    }
    int threads = 4;
    try { threads = std::stoi(manapi::process::get_env("MANAPIHTTP_THREADS").unwrap()); }
    catch (...) {  }

    manapi::async::context::threadpoolfs(threads);
    manapi::async::context::gbs = manapi::async::context::blockedsignals();

    int loops = 0;
    try { loops = std::stoi(manapi::process::get_env("MANAPIHTTP_LOOPS").unwrap()); }
    catch (...) {  }

    auto ctx = manapi::async::context::create(loops).unwrap();
    ctx->eventloop()->setup_handle_interrupt();


    std::atomic<int> a = 0;
    std::atomic<int> thrcnt = 0;


    // auto grpc_server_ctx = manapi::net::wgrpc::server_ctx::create().unwrap();
    auto server_ctx = manapi::net::http::server_ctx::create().unwrap();
    //
    // grpc::EnableDefaultHealthCheckService(true);
    // grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    manapi::async::context::run(ctx, loops, [&thrcnt, &a, server_ctx/*,grpc_server_ctx*/] (const std::function<void()> &bind) -> void {
        using http = manapi::net::http::server;
       // auto db = manapi::ext::pq::connection::create().unwrap();

        /**
         * grpc
         */

        // auto service = std::make_shared<GreeterServiceImpl>();

        // auto grpc_server = manapi::net::wgrpc::server::create (grpc_server_ctx).unwrap();
        // manapi::async::run([grpc_server, service] () mutable -> manapi::future<> {
        //     auto res = co_await grpc_server.config("/home/Timur/Desktop/WorkSpace/ManapiHTTP/cmake-build-debug/grpc.json");
        //
        //     res.log();
        //
        //     res = co_await grpc_server.start([&] (grpc::ServerBuilder &builder) -> manapi::error::status {
        //         builder.RegisterService(service.get());
        //         return manapi::error::status_ok();
        //     });
        //
        //     res.log();
        //     if (res.ok()) {
        //         manapi::async::current()->timerpool()->append_interval_async(100, [] (const manapi::timer &t) -> manapi::future<> {
        //             auto creds = co_await manapi::net::wgrpc::secure_channel_credentials("/home/Timur/Documents/ssl/quic/cert.crt");
        //             if (!creds.ok()) {
        //                 creds.err().log();
        //                 co_return;
        //             }
        //             GreeterClient greeter(grpc::CreateChannel("localhost:8080", creds.unwrap()));
        //             std::string user = "Xiadnoring Client";
        //             auto res = co_await greeter.SayHello(user);
        //             if (res.ok())
        //                 std::cout << res.unwrap() << "\n";
        //             else
        //                 res.err().log();
        //         });
        //     }
        //
        // }, [] (std::exception_ptr err) -> void {
        //     if (err)
        //         std::rethrow_exception(err);
        // });

        /**
         * http
         */

        auto folder_env = manapi::process::get_env("MANAPIHTTP_FOLDER");
        std::string const folder = folder_env ? FOLDER : FOLDER2;
        auto router = manapi::net::http::server::create (server_ctx).unwrap();

        router.GET("/+layer", [] (http::req &req, http::uresp resp) -> void {
            resp->header(std::string{"alt-svc"}, R"(h3=":8888"; ma=86400)");
            resp.finish();
        }).unwrap();

        router.GET("/stat", [server_ctx, &a] (http::req &req, http::resp &resp) mutable -> manapi::future<> {
            co_return resp.text(std::format("ip: {} port: {} online: {} requests: {}",
                req.ip_data().ip,
                req.ip_data().port,
                server_ctx.storage().as<manapi::net::http::server_ctx::worker_data_t>()->count.load(),
                a.load())).unwrap();
        }).unwrap();

        router.GET ("/main", [&a] (manapi::net::http::request &req, manapi::net::http::uresponse resp)
            -> void {
            a.fetch_add(1);
            resp->text("");

            resp.finish();
        }).unwrap();

        router.POST("/trailer", [] (http::req &req, http::resp &resp) -> manapi::future<void> {
            auto data = (co_await req.text()).unwrap();
            auto trailers = (co_await req.trailers()).unwrap();
            for (auto &trailer : trailers)
                printf("%.*s\n", trailer.second.size(), trailer.second.data());
            resp.text("hello");
        }, {
            {"trailers", manapi::json::array({"test", "HMMM"})},
            {"trailers_size", 500}
        });

        router.GET ("/fetch", [&a] (manapi::net::http::request &req, manapi::net::http::uresponse resp)
            -> void {

            resp->proxy("https://www.wikipedia.org", [] (manapi::net::fetch &n) -> void {
                n.verbose(true);
                n.headers({{"user-agent", R"(Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/139.0.0.0 Safari/537.36)"}});
            }).unwrap();

            resp.finish();
        }).unwrap();

        router.GET ("/stop", [] (http::req &req, manapi::net::http::uresponse resp) mutable -> void {
            resp->text("OK");

            manapi::async::run (manapi::async::current()->stop());

            resp.finish();
        }).unwrap();

        router.GET ("/timer", [&a] (manapi::net::http::request &req, manapi::net::http::uresponse resp)
            -> void {
            manapi::async::current()->timerpool()->append_timer_sync(1500,
                [resp = std::move(resp)] (manapi::timer t) mutable  -> void {
                    resp.finish();
            }).unwrap();
        }).unwrap();

        // router.GET("/pq/[id]", [db](manapi::net::http::request& req, manapi::net::http::response& resp) mutable -> manapi::future<> {
        //     auto msg = req.param("id").unwrap();
        //     char *end;
        //     auto res1 = co_await db.exec("INSERT INTO for_test (id, str_col) VALUES ($2, $1);","no way", std::strtoll(msg.data(), &end, 10));
        //     if (!res1) {
        //         if (res1.sqlcode() != manapi::ext::pq::SQL_STATE_UNIQUE_VIOLATION)
        //             res1.err().log();
        //     }
        //
        //     auto res = co_await db.exec("SELECT * FROM for_test;");
        //     if (res) {
        //         std::string content = "b";
        //         for (const auto &row: res.unwrap()) {
        //             content += std::to_string(row["id"].as<int>()) + " - " + row["str_col"].as<std::string>() + "<hr/>";
        //         }
        //
        //         co_return resp.text(std::move(content)).unwrap();
        //     }
        //
        //     co_return resp.text(std::string{res.is_sqlerr() ? res.sqlmsg() : res.message()}).unwrap();
        // }).unwrap();

        router.GET ("/free", [&a] (manapi::net::http::request &req, manapi::net::http::response &resp)
            -> manapi::future<> {
            manapi::init_tools::ev_library_init();
            manapi::async::current()->memory_fabric().clear();
            resp.compress_enabled(false);
            co_return resp.text(std::format("requests: {}; active: {}", a.load(),
                resp.connection_data()->worker->worker_data()->as<manapi::net::http::server_ctx::worker_data_t>()->count.load())).unwrap();
        }).unwrap();

        init_http_server (router, folder);

        manapi::async::run([router/*, db*/] () mutable -> manapi::future<> {
            //(co_await db.connect("127.0.0.1", "7879", "development", "rv8FY--PHz_QV<wvT4=n_Ru+cUJE}>KCqmBj9&#M3\\\"Gb.tx", "workflow-main")).unwrap();

            (co_await router.config("/home/Timur/Desktop/WorkSpace/ManapiHTTP/cmake-build-debug/config.json")).unwrap();
            (co_await router.start()).unwrap();
        });

        bind();
    }).unwrap();

    manapi::clear_tools::curl_library_clear();
    manapi::clear_tools::ev_library_clear();
    manapi::clear_tools::ssl_library_clear();

    return 0;
}
