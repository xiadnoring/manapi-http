#include <ManapiHttp.hpp>
#include <ManapiInitTools.hpp>

int main () {
    /* creates 2 threads for blocking I/O syscalls */
    manapi::async::context::threadpoolfs(2);
    /* disable several signals */
    manapi::async::context::gbs (manapi::async::context::blockedsignals());
    /* creates 4 additional threads for 4 additional event loops */
    auto ctx = manapi::async::context::create(4).unwrap();
    /* HTTP context for multiple HTTP routers (threadsafe) */
    auto router_ctx = manapi::net::http::server_ctx::create().unwrap();
    /* runs main event loop and 4 additional event loops */
    ctx->run(4, [router_ctx] (std::function<void()> bind) -> void {
        using http = manapi::net::http::server;

        auto router = manapi::net::http::server::create(router_ctx).unwrap();

        router.GET("/[test]/+custom", [] (http::req &req, http::uresp resp) mutable -> void {
            resp->text(std::format("{}, test={}", std::string{req.url()}, req.param("test").unwrap()));
        }).unwrap();

        router.GET("/user/[zone]-[id]", [] (http::req &req, http::uresp resp) mutable -> void {
            resp->text(std::format("zone={} id={}", req.param("zone").unwrap(),
                req.param("id").unwrap())).unwrap();
        }).unwrap();

        manapi::async::run([router] () mutable -> manapi::future<> {
            manapi::unwrap(co_await router.config_object({
                {"pools", manapi::json::array({
                    {
                        {"address", "127.0.0.1"},
                        {"http", manapi::json::array({"1.1"})},
                        {"port", "8888"}
                    }
                })},
                {"save_config", false}
            }));

            manapi::unwrap(co_await router.start());
        });

        /* bind event loop in the current context */
        bind();
    }).unwrap();

    manapi::clear_tools::curl_library_clear();
    manapi::clear_tools::ev_library_clear();
    manapi::clear_tools::ssl_library_clear();

    return 0;
}