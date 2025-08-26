#include "test_fetch.hpp"


#ifdef MANAPIHTTP_HTTP_AS_EXECUTABLE
#   include "ManapiFetch2.hpp"
#   include "ManapiString.hpp"
#else
#   include <manapihttp/ManapiFetch2.hpp>
#   include <manapihttp/ManapiString.hpp>
#endif
#include "./utest.h"


#define HTTP1PORT "8888"

manapi::async::shared_ctx init_ctx () {
    auto ctx = manapi::async::context::create(4).unwrap();
    ctx->eventloop()->setup_handle_interrupt();
    return ctx;
}

void wait_ctx (manapi::async::shared_ctx ctx) {
    manapi::async::context::run(ctx, 0, [] (std::function<void()> bind) -> void {

        bind();
    });
}

manapi::net::http::server init_router (manapi::json cnf, std::move_only_function<manapi::future<>()> cb) {
    using http = manapi::net::http::server;

    auto server = manapi::net::http::server_ctx::create();
    auto router = manapi::net::http::server::create(server.unwrap()).unwrap();

    router.GET ("/", [] (http::req &req, http::uresp resp) -> void {
        resp->text("Hello, World!");
    }).unwrap();

    manapi::async::run ([router, cnf, cb = std::move(cb)] () mutable -> manapi::future<> {
        manapi::json config = {
            {"pools", manapi::json::array()}
        };
        if (cnf.contains("http1") && cnf["http1"].as_bool_cast()) {
            config["pools"].push_back({
                {"address", "127.0.0.1"},
                {"port", HTTP1PORT},
                {"http", manapi::json::array("1.1")},
                {"transport", "tcp"},
                {"tcp_no_delay", true},
                {"simultaneous_accepts", true},
                {"buffer_size", 4096},
                {"max_buffer_stack", 1},
                {"max_merge_buffer_stack", 1},
                {"max_connections", 2},
                {"max_connections_by_ip", 2}
            });
        }
        auto res = co_await router.config_object (config);
        res.unwrap();

        res = co_await router.start();
        res.unwrap();

        co_await cb();

        co_await manapi::async::current()->stop();
    });

    return router;
}

UTEST(http_and_fetch, simple_request) {
    auto ctx = init_ctx();
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT, {
            {"method", "GET"}
        });

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

        auto data_res = co_await fetch.text();
#define return co_return
        ASSERT_TRUE_MSG((data_res.unwrap() == "Hello, World!"), "check response data");
#undef return
    });

    wait_ctx(ctx);
}

UTEST(http_and_fetch, simple_post_request) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx();
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/post", {
            {"method", "POST"}
        }, "Hello, World!");

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

        auto data_res = co_await fetch.text();
#define return co_return
        ASSERT_TRUE_MSG((data_res.unwrap() == "OK"), "check response data");
#undef return
    });

    router.POST ("/post", [&] (http::req &req, http::resp &resp) -> manapi::future<> {
         auto data = co_await req.text();
#define return co_return
        ASSERT_TRUE_MSG((data.unwrap() == "Hello, World!"), "check post data");
#undef return
        co_return resp.text("OK").unwrap();
    }).unwrap();

    wait_ctx(ctx);
}

UTEST(http_and_fetch, callback_sync_get_request) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx();
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/callback", {
            {"method", "GET"}
        });

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

        auto data_res = co_await fetch.callback_sync([] (char *buffer, ssize_t size) -> ssize_t {
            for (size_t i =0 ; i < size; i++) {
                if (buffer[i] != '2')
                    return -1;
            }
            return size;
        });
#define return co_return
        ASSERT_TRUE_MSG((data_res.ok()), "check response data");
#undef return
    });

    router.GET ("/callback", [&] (http::req &req, http::resp &resp) -> manapi::future<> {
         resp.header(std::string{manapi::net::http::header::CONTENT_LENGTH}, "100000").unwrap();
         co_return resp.callback_sync([left = ssize_t(100000)] (char *buffer, ssize_t size, bool &fin) mutable -> ssize_t {
             auto const copy = std::min<ssize_t>(left, size);
             memset(buffer, '2', copy);
             left -= copy;
             if (!left)
                 fin = true;
             return copy;
         }).unwrap();
    }).unwrap();

    wait_ctx(ctx);
}

UTEST(http_and_fetch, callback_async_get_request) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx();
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/callback", {
            {"method", "GET"}
        });

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

        size_t read = 0;
        auto data_res = co_await fetch.callback_async([&read] (manapi::slice_view buffs, bool fin) -> manapi::future<ssize_t> {
            if (read > 180000)
                co_await manapi::async::delay{100};

            for (auto it = buffs.begin(); it != buffs.end(); it++) {
                auto const s = (char*)it.buffer();
                auto j = (read % 100);
                for (size_t i = 0 ; i < it.size(); i++) {
                    if (i == j) {
                        if (s[j] != '3')
                            co_return -1;
                        j += 100;
                        continue;
                    }
                    if (s[i] != '2')
                        co_return -1;
                }
                read += it.size();
            }
            co_return buffs.size();
        });
#define return co_return
        ASSERT_TRUE_MSG((data_res.ok() && read == 300000), "check response data");
#undef return
    });

    router.GET ("/callback", [&] (http::req &req, http::resp &resp) -> manapi::future<> {
         resp.header(std::string{manapi::net::http::header::CONTENT_LENGTH}, "300000").unwrap();
         co_return resp.callback_async([left = ssize_t(300000)] (manapi::slice_view buffs, bool &fin) mutable -> manapi::future<ssize_t> {
             size_t res = 0;
             for (auto it = buffs.begin(); it != buffs.end(); it++) {
                 auto const copy = std::min<ssize_t>(left, it.size());
                 memset (it.buffer(), '2', copy);
                 // 0->0 73->100 100->100
                 auto i = ((300000 - left) % 100);
                 auto cc = (char*)it.buffer();
                 for (; i < copy; i+=100)
                     (cc)[i] = '3';
                 if (left > 70000) {
                     co_await manapi::async::delay{100};
                 }
                 left -= copy;
                 res += copy;
                 if (!left) {
                     fin = true;
                     break;
                 }
             }
             co_return res;
         }).unwrap();
    }).unwrap();

    wait_ctx(ctx);
}

UTEST(http_and_fetch, formdata_request) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx();
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        manapi::net::fetch_formdata formdata;
        formdata.set_text("hello", "msg").unwrap();
        formdata.set_text("hello2", "msg2").unwrap();
        formdata.set_text("1", manapi::string::fill(500, 'A'));
        formdata.set_text("hello3", "msg3").unwrap();
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/formdata", {
            {"method", "POST"}
        }, std::move(formdata));

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

        auto data_res = co_await fetch.text();
#define return co_return
        ASSERT_TRUE_MSG((data_res.unwrap() == "OK"), "check response data");
#undef return
    });

    router.POST ("/formdata", [&] (http::req &req, http::resp &resp) -> manapi::future<> {
        std::string msg1, msg2, msg3;
        auto data = co_await req.form([&msg1, &msg2, &msg3] (std::string name) -> manapi::net::formdata_recv::ondata_cb_t {
            std::string *s{nullptr};
            if (name == "hello")
                s = &msg1;
            else if(name == "hello2")
                s = &msg2;
            else if (name=="hello3")
                s = &msg3;

            if (s)
                return manapi::net::formdata_recv::save_string(s);

            return [res = size_t(0)] (manapi::slice_view buffs, bool fin) mutable -> manapi::future<ssize_t> {
                for (auto it = buffs.begin(); it != buffs.end(); it++) {
                    char const *c = (const char*)it.buffer();
                    for (int i = 0; i < it.size(); i++) {
                        if (c[i] != 'A')
                            co_return -1;
                    }
                    res+=it.size();
                }
                if (fin) {
                    if (res != 500)
                        co_return -1;
                }
                co_return buffs.size();
            };
        });
        data.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((msg1 == "msg"), "check response data 1");
        ASSERT_TRUE_MSG((msg2 == "msg2"), "check response data 2");
        ASSERT_TRUE_MSG((msg3 == "msg3"), "check response data 3");
#undef return
        co_return resp.text("OK").unwrap();
    }).unwrap();

    wait_ctx(ctx);
}

UTEST(http_and_fetch, formdata_response) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx();
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/formdata", {
            {"method", "GET"}
        });

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

        auto data_res = co_await fetch.callback_sync([] (char *buffer, ssize_t size) -> ssize_t {
            /**
             * TODO: cURL FormData support with some tests
             */
            return size;
        });
    });

    router.GET ("/formdata", [&] (http::req &req, http::resp &resp) -> manapi::future<> {
         manapi::net::formdata_send send;
         send.set_text("hello", "world").unwrap();
         send.set_text("hello2", "world2").unwrap();
         co_return resp.form(std::move(send)).unwrap();
    }).unwrap();

    wait_ctx(ctx);
}