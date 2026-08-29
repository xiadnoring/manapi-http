#include "ManapiHttp.hpp"
#include "include/ManapiFetch2.hpp"
#include "ext/pq/PostgreClient.hpp"
#include "ext/pq/PostgrePool.hpp"
#ifdef _WIN32
#   define FOLDER ".\\data\\"
#   define FOLDER2 ".\\data\\"
#else
// #define FOLDER "/home/Timur/Downloads/RPG"
// #define FOLDER2 "/home/Timur/Downloads/RPG"
//#define FOLDER "/home/Timur/Downloads/anime-main/"
#define FOLDER2 "/home/timur/Документы/http2priorities/"
#define FOLDER "/home/timur/Документы/http2priorities/"
#endif
#include <cstring>

#include "handlers.hpp"
#include "crypto/ManapiAEAD.hpp"
#include "hash/ManapiSHA256.hpp"
#include "ManapiInitTools.hpp"
#include "ManapiMath.hpp"
#include "ManapiProcess.hpp"
#include "ManapiString.hpp"
#include "std/ManapiTimer.hpp"
#include "std/ManapiEasyCancellation.hpp"
#include "ext/ManapiMustache.hpp"

//#include "protobuf/helloworld.grpc.pb.h"
//#include "ManapiGrpc.hpp"

#include "include/std/ManapiFunction.hpp"
//
#include "std/ManapiRef.hpp"

//static std::atomic<std::size_t> bbbb = 0;
//
//// Logic and data behind the server's behavior.
//class GreeterServiceImpl final : public helloworld::Greeter::CallbackService {
//    manapi::async::shared_cthread ctx;
//public:
//    GreeterServiceImpl () {
//        ctx = manapi::async::current();
//    }
//
//    ~GreeterServiceImpl() override {
//        std::cout << "RESET GreeterServiceImpl\n";
//    }
//    grpc::ServerUnaryReactor *SayHello(grpc::CallbackServerContext* context, const helloworld::HelloRequest* request,
//                    helloworld::HelloReply* reply) override {
//        grpc::ServerUnaryReactor* reactor = context->DefaultReactor();
//        auto cb = std::function ([reactor, reply, request] () -> void {
//             manapi::async::run ([reactor, reply, request] () -> manapi::future<> {
//                manapi::ctoken check_timeout;
//                check_timeout.cancel_callback([] () -> void {
//                    std::cout << (size_t)bbbb <<  " IS TOO SLOW\n";
//                });
//                check_timeout.timeout(7000);
//                try {
//                    bbbb.fetch_add(1);
//                    manapi::json zz = manapi::json::object();
//                    zz.insert({"http", "2"});
//                    zz.insert({"verify_peer", false});
//                    zz.insert({"verify_host", false});
//                    auto status = co_await manapi::net::fetch2::fetch ("https://localhost:8885/stat",std::move(zz), manapi::ctokens::timeout(2000));
//
//                    if (status.ok()) {
//                        auto response = status.unwrap();
//                        if (response->ok()) {
//                            reply->set_message(std::format("Hello, {}! Fact: {}", request->name(), (co_await response->text()).unwrap()));
//                        }
//                        else {
//                            reply->set_message(std::format("Hello, {}! Something gets wrong. status: {}", request->name(), response->status()));
//                        }
//                    }
//                    else {
//                        reply->set_message(std::format("Hello, {}! Something gets wrong. status: {}", request->name(), status.message()));
//                    }
//                }
//                catch (std::exception const &e) {
//                    reply->set_message(std::format("Hello, {}! Something gets wrong: {}", request->name(), e.what()));
//                }
//                bbbb.fetch_sub(1);
//                check_timeout.disable();
//                reactor->Finish(grpc::Status::OK);
//                co_return;
//            });
//        });
//        if (manapi::async::internal::current_()) {
//            cb();
//        }
//        else {
//            ctx->eventloop()->custom_callback ([cb = std::move(cb)] (manapi::event_loop* zev) mutable -> void {
//                cb();
//            });
//        }
//        return reactor;
//    }
//};
//
//class GreeterClient {
//public:
//    GreeterClient(std::shared_ptr<grpc::Channel> channel)
//        : stub_(helloworld::Greeter::NewStub(channel)) {}
//
//    ~GreeterClient() {
//        std::cout << "RESET GreeterClient\n";
//    }
//
//    // Assembles the client's payload, sends it and presents the response back
//    // from the server.
//    manapi::future<manapi::status_or<std::string>> SayHello(const std::string& user) {
//        typedef manapi::async::promise_sync<manapi::status_or<std::string>> promise;
//        // Data we are sending to the server.
//        helloworld::HelloRequest request;
//        request.set_name(user);
//
//        // Container for the data we expect from the server.
//        helloworld::HelloReply reply;
//
//        // Context for the client. It could be used to convey extra information to
//        // the server and/or tweak certain RPC behaviors.
//        grpc::ClientContext context;
//
//        co_return co_await promise ([&] (promise::resolve_t resolve) -> void {
//            try {
//                this->stub_->async()->SayHello(&context, &request, &reply, [ctx = manapi::async::current(), &reply, resolve = std::move(resolve)] (grpc::Status status) mutable {
//                    if (manapi::async::internal::current_() == ctx) {
//                        if (status.ok()) {
//                            resolve(reply.message());
//                            return;
//                        }
//
//                        auto msg = status.error_message();
//                        manapi_log_debug("grpc client failed due to %s", msg.data());
//                        resolve(manapi::status_internal("grpc client: something gets wrong"));
//                    }
//                    else {
//                        ctx->eventloop()->custom_callback([resolve = std::move(resolve), status = std::move(status), &reply] (manapi::event_loop *ev) -> void {
//                            if (status.ok()) {
//                                resolve(reply.message());
//                                return;
//                            }
//
//                            auto msg = status.error_message();
//                            manapi_log_debug("grpc client failed due to %s", msg.data());
//                            resolve(manapi::status_internal("grpc client: something gets wrong"));
//                        }).unwrap();
//                    }
//                });
//            }
//            catch (std::exception const &e) {
//                manapi_log_error(e.what());
//                resolve(manapi::status_internal("sayhello failed"));
//            }
//        });
//    }
//
//private:
//    std::unique_ptr<helloworld::Greeter::Stub> stub_;
//};


int main () {

    int stmax = 300;
    try { stmax = std::stoi(manapi::process::get_env("MANAPIHTTP_STMAX").unwrap()); }
    catch (...) {  }

    manapi::init_tools::max_coro_stack((size_t)stmax);

    int logtrace = 4;
    try { logtrace = std::stoi(manapi::process::get_env("MANAPIHTTP_LOGTRACE").unwrap()); }
    catch (...) {  }

    manapi::init_tools::log_trace_init((manapi::debug::trace_level)logtrace);

    size_t threads = 4;
    try { threads = (size_t)std::stoi(manapi::process::get_env("MANAPIHTTP_THREADS").unwrap()); }
    catch (...) {  }

    manapi::async::context::threadpoolfs(threads);
    manapi::async::context::gbs(manapi::async::context::blockedsignals());

    size_t loops = 0;
    try { loops = (size_t)std::stoi(manapi::process::get_env("MANAPIHTTP_LOOPS").unwrap()); }
    catch (...) {  }

    manapi::init_tools::log_name_enable("manapihttp", true);
    manapi::init_tools::log_name_enable("manapihttp::grpc", true);

    auto ctx = manapi::async::context::create(loops + 1).unwrap();

    std::atomic<int> a = 0;
    std::atomic<int> thrcnt = 0;


//    auto grpc_server_ctx = manapi::net::wgrpc::server_ctx::create().unwrap();
    auto server_ctx = manapi::net::http::server_ctx::create().unwrap();

//    grpc_server_ctx->enable_threadpool(false);
//
//    grpc::EnableDefaultHealthCheckService(true);
//    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    ctx->run(loops, [&thrcnt, &a, server_ctx/*,grpc_server_ctx*/] (const std::function<void()> &bind) -> void {

            using http = manapi::net::http::server;
            auto pool = manapi::ext::pq::pool::create().unwrap();
            auto db = manapi::ext::pq::db::create().unwrap();

            db->add_slave(pool).unwrap();

            // /**
            //  * grpc
            //  */
            //
            auto thrcntind = thrcnt.fetch_add(1);


//            auto grpc_server = manapi::net::wgrpc::server::create (grpc_server_ctx).unwrap();
//            manapi::async::run([grpc_server, thrcntind] () mutable -> manapi::future<> {
//                auto res = co_await grpc_server->config("/home/timur/Рабочий стол/WorkSpace/ManapiHTTP/cmake-build-debug/grpc.json");
//
//                res.log();
//
//
//                auto service = std::make_unique<GreeterServiceImpl>();
//                res = co_await grpc_server->start([] (grpc::ServerBuilder &builder, grpc::Service *service) -> manapi::status {
//                    builder.RegisterService(service);
//                    return manapi::status_ok();
//                }, service.release());
//
//                res.log();
//                assert(res.ok());
//                if (res.ok()) {
//                    auto creds = co_await manapi::net::wgrpc::secure_channel_credentials("/home/timur/Документы/ssl/quic/cert.crt");
//                    if (!creds.ok()) {
//                        creds.err().log();
//                        co_return;
//                    }
//
//                    auto channel= creds.unwrap();
//                    auto greeter = std::make_shared<GreeterClient>(grpc::CreateChannel("localhost:8080", channel));
//                            manapi::async::current()->timerpool()->append_timer_async(100, [greeter] (manapi::timer t) -> manapi::future<> {
//                                std::string user = "Xiadnoring Client #1";
//                                auto res = co_await greeter->SayHello(user);
//                                if (res.ok())
//                                    std::cout << "Xiadnoring Client#1 =" << res.unwrap() << "\n";
//                                else
//                                    res.err().log();
//                            }).unwrap();
//                    }
//
//                    co_return;
//                });

            /**
             * http
             */

            auto folder_env = manapi::process::get_env("MANAPIHTTP_FOLDER");
            std::string const folder = folder_env ? FOLDER : FOLDER2;
            auto router = manapi::net::http::server::create (server_ctx).unwrap();

            router->GET("/+layer", [] (http::req &req, http::uresp resp) -> void {
                resp->header(std::string{"alt-svc"}, R"(h3=":8888"; ma=86400)");
                resp.finish();
            }).unwrap();

            router->GET ("/cf", [] (http::req &req, http::resp &resp) -> manapi::future<> {
                manapi::json params = manapi::json::object();
                params.insert({"method", "GET"});
                auto response = manapi::unwrap(co_await manapi::net::fetch2::fetch ("https://codeforces.com/api/contest.list?gym=true",
                        std::move(params), req.cancellation().sub().tm(5000)));

                auto data = manapi::unwrap(co_await response->slice ());
                auto p = manapi::json::parse( data ).unwrap();

                if (p["status"] == "OK") {
                    auto &result = p["result"];
                    auto names = manapi::json::array();

                    for (const auto &z : result.each()) {
                        names.push_back ( z["name"] );
                    }

                    co_return resp.json ( std::move(names), 4 ).unwrap();
                }
                else {
                    co_return resp.text(std::format("status = {}", p["status"].as_string())).unwrap();
                }

            }).unwrap();

            router->GET("/stat", [server_ctx, &a] (http::req &req, http::resp &resp) mutable -> manapi::future<> {
                co_return resp.text(std::format("ip: {} port: {} online: {} requests: {}",
                    req.ip_data().ip,
                    req.ip_data().port,
                    server_ctx->storage().as<manapi::net::http::server_ctx::worker_data_t>()->count.load(),
                    a.load())).unwrap();
            }).unwrap();

            router->GET ("/db/insert", [db] (http::req &req, http::resp &resp) -> manapi::future<> {
                try {
                    auto res = manapi::unwrap(co_await db->execl(manapi::ext::pq::kSlave,
                                                                 "INSERT INTO for_test (id, name, year, day, is_verified, dd) "
                                                                 "VALUES ($1, $2, $3, $4, $5, $6);",
                                                                 req.cancellation().sub().tm(5000),
                                                                 std::stoll(req.get_extract("id").unwrap().second),
                                                                 req.get("name").unwrap(),
                                                                 std::stol(req.get_extract("year").unwrap().second),
                                                                 std::stol(req.get_extract("day").unwrap().second),
                                                                         (req.get("is_verified").unwrap() == "true"),
                                                                 std::numeric_limits<double>::max()));


                }
                catch (std::exception const &e) {
                    manapi_log_trace(e.what());
                }

                co_return resp.text("finished!").unwrap();
            });

            router->GET ("/main", [&a] (manapi::net::http::request &req, manapi::net::http::uresponse resp)
                -> void {
                a.fetch_add(1);
                resp->text("");

                resp.finish();
            }).unwrap();

            router->POST("/trailer", [] (http::req &req, http::resp &resp) -> manapi::future<void> {
                auto data = (co_await req.text()).unwrap();
                auto trailers = (co_await req.trailers()).unwrap();
                for (auto &trailer : trailers)
                    printf("%.*s\n", trailer.second.size(), trailer.second.data());
                resp.text("hello");
            }, {
                {"trailers", manapi::json::array({"test", "HMMM"})},
                {"trailers_size", 500}
            });

            router->GET ("/fetch/+custom", [&a] (manapi::net::http::request &req, manapi::net::http::response &resp)
                -> manapi::future<> {

                auto path = std::vector<std::string> (std::next(req.path().begin()), req.path().end());
                std::string url = "";
                for (auto &p : path) {
                    url += '/';
                    url += p;
                }

                manapi::json pp = manapi::json::object();
                manapi::json hdrs = manapi::json::object();
                hdrs.insert({"user-agent", R"(Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/139.0.0.0 Safari/537.36)"});
                pp.insert({"verify_peer", false});
                pp.insert({"verify_host", false});
                pp.insert({"verbose", true});
                pp.insert({"http", 2});
                pp.insert({"headers", std::move(hdrs)});

                auto response = (co_await manapi::net::fetch2::fetch(std::format("https://localhost:8885/{}", url),
                                                                     std::move(pp))).unwrap();
                if (!response->ok()) {
                    co_return resp.text(std::string{manapi::net::http::status_to_string(response->status()).unwrap()}).unwrap();
                }

                co_return resp.text((co_await response->text()).unwrap()).unwrap();
            }).unwrap();

            router->GET ("/stop", [] (http::req &req, manapi::net::http::uresponse resp) mutable -> void {
                resp->text("OK");

                manapi::async::run (manapi::async::current()->stop());

                resp.finish();
            }).unwrap();

            router->GET ("/timer", [&a] (manapi::net::http::request &req, manapi::net::http::uresponse resp)
                -> void {
                manapi::async::current()->timerpool()->append_timer_sync(1500,
                    [resp = std::move(resp)] (manapi::timer t) mutable  -> void {
                        resp.finish();
                }).unwrap();
            }).unwrap();

            // router->GET("/pq/[id]", [db](manapi::net::http::request& req, manapi::net::http::response& resp) mutable -> manapi::future<> {
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

            router->POST ("/slice", [&a] (manapi::net::http::request &req, manapi::net::http::response &resp)
                -> manapi::future<> {
                try {
                    auto txt = manapi::unwrap(co_await req.slice());
                    auto res = manapi::json::parse(txt, manapi::JSON_FLAG_SLICES).unwrap();
                    co_return resp.json(std::move(res)).unwrap();
                }
                catch (std::exception const &e) {
                    manapi_log_debug(e.what());
                    std::rethrow_exception(std::current_exception());
                }
            }).unwrap();

            router->GET ("/slice", [&a] (manapi::net::http::request &req, manapi::net::http::response &resp)
                -> manapi::future<> {
                try {
                    std::string p = "/home/Timur/Documents/ielcfinal/test_1gb.json";
                    auto f = manapi::unwrap(co_await manapi::fs::async_open(p, manapi::ev::FS_O_RDONLY, 0755, req.cancellation().sub()));
                    auto sz = manapi::unwrap(co_await manapi::fs::async_file_size(p, req.cancellation().sub()));
                    auto sv = manapi::async::memory_fabric()->slice(sz).unwrap();
                    auto rd = manapi::unwrap(co_await manapi::fs::async_read(f.get(), sv, -1, req.cancellation().sub()));
                    if (rd < 0) co_return resp.text("failed to read").unwrap();
                    auto res = manapi::json::parse(sv, manapi::JSON_FLAG_SLICES).unwrap();
                    // co_return resp.json(std::move(res)).unwrap();
                    co_return resp.text("hello").unwrap();
                }
                catch (std::exception const &e) {
                    manapi_log_debug(e.what());
                    std::rethrow_exception(std::current_exception());
                }
            }).unwrap();

            router->GET ("/free", [&a] (manapi::net::http::request &req, manapi::net::http::response &resp)
                -> manapi::future<> {
                manapi::init_tools::ev_library_init();
                manapi::async::current()->memory_fabric().clear();
                resp.compress_enabled(false);
                co_return resp.text(std::format("requests: {}; active: {}", a.load(),
                    resp.connection_data()->worker->worker_data()->as<manapi::net::http::server_ctx::worker_data_t>()->count.load())).unwrap();
            }).unwrap();

            init_http_server (router, folder);

            manapi::async::run([router/*, db*/, pool] () mutable -> manapi::future<> {
                //(co_await db.connect("127.0.0.1", "7879", "development", "rv8FY--PHz_QV<wvT4=n_Ru+cUJE}>KCqmBj9&#M3\\\"Gb.tx", "workflow-main")).unwrap();

                manapi::unwrap(co_await pool->connect(3, "127.0.0.1", "5432", "storeuser", "1234", "manapistore"));

                (co_await router->config("/home/timur/Рабочий стол/WorkSpace/ManapiHTTP/cmake-build-debug/config.json")).unwrap();
                (co_await router->start()).unwrap();

                manapi_log_trace(manapi::debug::LOG_TRACE_HIGH, "http server has been started");
            });

            bind();

    }).unwrap();

    manapi::clear_tools::clear_all();

    std::cout << "eventloop" << " " << ctx->eventloop().use_count() - 1 << "\n";
    std::cout << "ctx" << " " << ctx.use_count() - 1 << "\n";

    return 0;
}
