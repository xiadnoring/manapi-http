#include "ManapiInitTools.hpp"
#include "ManapiProcess.hpp"
#include "ManapiFetch2.hpp"
#include "ManapiString.hpp"
#include "ManapiHttp.hpp"
#include "json/ManapiJson.hpp"
#include "std/ManapiEasyCancellation.hpp"


#include "./utest.h"
#include "./tools.hpp"

UTEST(http, http_router_exists) {
    using http = manapi::net::http::server;

    auto server = manapi::net::http::server_ctx::create();
    auto router = manapi::net::http::server::create(server.unwrap()).unwrap();

    router.GET ("/hello", [] (http::req &req, http::uresp resp) -> void {
        resp.finish();
    }).unwrap();

    auto res = router.GET ("/hello", [] (http::req &req, http::uresp resp) -> void {
        resp.finish();
    });

    ASSERT_TRUE_MSG(!res.ok(), "already exists");
}

#ifdef MANAPIHTTP_FETCH_SUPPORT

UTEST(http_and_fetch, simple_request) {
    auto ctx = init_ctx(utest_result);
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        manapi::json jparams = {
            {"method", "GET"}
        };
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT, std::move(jparams));

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

    auto ctx = init_ctx(utest_result);
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        manapi::json jparams = {
            {"method", "POST"}
        };
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/post", std::move(jparams), "Hello, World!");

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

    auto ctx = init_ctx(utest_result);
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        manapi::json jparams = {
            {"method", "GET"}
        };
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/callback", std::move(jparams));

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

    auto ctx = init_ctx(utest_result);
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        manapi::json jparams = {
            {"method", "GET"}
        };
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/callback", std::move(jparams));

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

        auto zz = manapi::string::fill(300000, '2');
        for (int i = 0; i < zz.size(); i+=100)
            zz[i] = '3';
        size_t read = 0;
        auto data_res = co_await fetch.callback_async([&, f =int(0)] (manapi::slice_view buffs, bool fin) mutable -> manapi::future<ssize_t> {
            manapi::slice tt{};
            tt.resize(buffs.size()).unwrap();
            tt.copy_from(buffs, 0, 0, buffs.size()).unwrap();
            if (read > 180000 && !f) {
                co_await manapi::async::delay{100};
                f = 1;
            }
#define return co_return -1
        ASSERT_TRUE_MSG((tt.cmp(buffs) == 0), "memory corruption was detected #1");
#undef return
            for (auto it = buffs.begin(); it != buffs.end(); it++) {
                auto const s = (char*)it.buffer();
                auto j = 100 - (read % 100);
                if (j == 100) j = 0;
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
#define return co_return -1
                ASSERT_TRUE_MSG(!memcmp(it.buffer(), zz.data() + read, it.size()), "memory corruption was detected #2");
#undef return
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
         co_return resp.callback_async([left = ssize_t(300000), f = bool(false)] (manapi::slice_view buffs, bool &fin) mutable -> manapi::future<ssize_t> {
             size_t res = 0;
             for (auto it = buffs.begin(); it != buffs.end(); it++) {
                 auto const copy = std::min<ssize_t>(left, it.size());
                 memset (it.buffer(), '2', copy);
                 // 0->0 73->100 100->100
                 auto i = 100 - ((300000 - left) % 100);
                 if ( i==100) i=0;
                 auto cc = (char*)it.buffer();
                 for (; i < copy; i+=100)
                     (cc)[i] = '3';
                 if (left > 70000 && !f) {
                     co_await manapi::async::delay{100};
                     f = true;
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

    auto ctx = init_ctx(utest_result);
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        manapi::net::fetch_formdata formdata;
        formdata.set_text("hello", "msg").unwrap();
        formdata.set_text("hello2", "msg2").unwrap();
        formdata.set_text("1", manapi::string::fill(500, 'A'));
        formdata.set_text("hello3", "msg3").unwrap();
        manapi::json jparams = {
            {"method", "POST"}
        };
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/formdata", std::move(jparams), std::move(formdata));

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

    auto ctx = init_ctx(utest_result);
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        manapi::json jparams = {
            {"method", "GET"}
        };
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/formdata", std::move(jparams));

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


UTEST(http_and_fetch, formdata_bad_response__no_data) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx(utest_result);
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        manapi::json jparams = {
            {"method", "GET"},
            {"verbose", false}
        };
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/bad", std::move(jparams), manapi::async::timeout_cancellation(5000));

        if (!fetch_res.ok())
            co_return;

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

        auto data_res = co_await fetch.callback_sync([] (char *buffer, ssize_t size) -> ssize_t {
            /* skip */
            return size;
        });

#define return co_return
        ASSERT_TRUE_MSG((!data_res.ok()), "check response bad");
#undef return

    });

    router.GET ("/bad", [&] (http::req &req, http::resp &resp) -> manapi::future<> {
        resp.header(std::string{manapi::net::http::header::CONTENT_LENGTH}, "100000");
        co_return resp.callback_stream([] (auto cb) -> manapi::future<> {
            char tt[99999];
            memset(tt, '1', sizeof (tt));
            manapi::slice c{};
            c.resize(sizeof (tt)).unwrap();
            c.copy_from(tt, 0, sizeof (tt)).unwrap();
            co_await cb (c, true);
        }).unwrap();
    }).unwrap();

    wait_ctx(ctx);
}

UTEST(http_and_fetch, formdata_bad_response) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx(utest_result);
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        manapi::json jparams = {
            {"method", "GET"},
            {"verbose", false}
        };
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" "443" "/bad", std::move(jparams), manapi::async::timeout_cancellation(5000));

        if (!fetch_res.ok())
            co_return;

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

        auto data_res = co_await fetch.callback_sync([] (char *buffer, ssize_t size) -> ssize_t {
            /* skip */
            return size;
        });

#define return co_return
        ASSERT_TRUE_MSG((!data_res.ok()), "check response bad");
#undef return

    });

    wait_ctx(ctx);
}

UTEST(http_and_fetch, chunked_request) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx(utest_result);
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        manapi::json jparams = {
            {"method", "GET"},
            {"verbose", false}
        };
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/chunked", std::move(jparams), manapi::async::timeout_cancellation(5000));

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

        auto data_res = co_await fetch.text();
        auto data = data_res.unwrap();

        for (int i = 0 ; i < data.size(); i++) {
#define return co_return
            ASSERT_TRUE_MSG((data[i] == (i % 10)), "memory corruption #1");
#undef return
        }

#define return co_return
        ASSERT_TRUE_MSG((data.size() == 100000), "memory corruption #2");
#undef return

    });

    router.GET ("/chunked", [&] (http::req &req, http::resp &resp) -> manapi::future<> {
        co_return resp.callback_stream([] (manapi::net::http::response::resp_stream_cb cb) -> manapi::future<> {
            char zz[100000];
            for(int i = 0; i < sizeof (zz); i++) {
                zz[i] = (char)(i % 10);
            }
            manapi::slice_ref ref;
            ref.push_back(zz, 20000);
            co_await cb (ref, false);

            ref.clear();
            ref.push_back(zz + 20000, 30000);
            co_await cb (ref, false);

            ref.clear();
            ref.push_back(zz + 50000, 50000);
            co_await cb (ref, true);
        }).unwrap();
    }).unwrap();

    wait_ctx(ctx);
}


UTEST(http_and_fetch, chunked_response) {
    using http = manapi::net::http::server;

    auto ctx = init_ctx(utest_result);
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {
        std::string zz;
        zz.resize(100000);
        for (int i = 0; i < zz.size(); i++)
            zz[i] = (char)(i % 10);
        manapi::json jparams = {
            {"method", "POST"},
            {"verbose", false}
        };
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/chunked", std::move(jparams), [&zz, cursor = int(0)] (manapi::slice_view buffs, bool &fin) mutable -> manapi::future<ssize_t> {
            auto const copy = std::min<std::size_t>(zz.size() - cursor, buffs.size());
            auto res = buffs.copy_from(zz.data() + cursor, 0, copy);
            if (!res) {
                res.log();
                co_return -1;
            }
            cursor += copy;
            if (cursor == zz.size())
                fin = true;
            co_return copy;
        }, manapi::async::timeout_cancellation(5000));

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return


    });

    router.POST ("/chunked", [&] (http::req &req, http::resp &resp) -> manapi::future<> {
        auto data_res = co_await req.text();
        auto data = data_res.unwrap();
        for (int i = 0 ; i < data.size(); i++) {
#define return co_return
            ASSERT_TRUE_MSG((data[i] == (i % 10)), "memory corruption #1");
#undef return
        }

#define return co_return
        ASSERT_TRUE_MSG((data.size() == 100000), "memory corruption #2");
#undef return
        co_return resp.text("OK").unwrap();
    }).unwrap();

    wait_ctx(ctx);
}

UTEST(http_and_fetch, user_data) {
    using http = manapi::net::http::server;

    struct user_data_for_test_t {
        std::string msg;
    };

    auto ctx = init_ctx(utest_result);
    auto router = init_router({
        {"http1", true}
    }, [&] () -> manapi::future<> {

        manapi::json jparams = {
            {"method", "GET"},
            {"verbose", false}
        };
        auto fetch_res = co_await manapi::net::fetch2::fetch("http://127.0.0.1:" HTTP1PORT "/admin/test", std::move(jparams), manapi::async::timeout_cancellation(5000));

        auto fetch = fetch_res.unwrap();

#define return co_return
        ASSERT_TRUE_MSG((fetch.ok()), "check response status");
#undef return

        auto data_res = co_await fetch.text();
        auto data = data_res.unwrap();
#define return co_return
        ASSERT_TRUE_MSG((data == "OK"), "check response data");
#undef return

    });

    router.GET ("/admin/+layer", [&] (http::req &req, http::uresp resp) -> void {
        req.propagation(true);
        manapi::net::http::custom_data_t cd;
        auto s = new user_data_for_test_t ();
        s->msg = "OK";
        cd.src = s;
        cd.clean = [] (void *data) -> void { delete (user_data_for_test_t *)data; };
        resp->custom_data(std::move(cd));
    }).unwrap();

    router.GET ("/admin/test", [&] (http::req &req, http::resp &resp) -> manapi::future<> {
        auto user_data = resp.custom_data_as<user_data_for_test_t>();
        co_return resp.text(std::move(user_data->msg)).unwrap();
    }).unwrap();

    wait_ctx(ctx);
}

#endif

MANAPIHTTP_TESTS_MAIN