#include "ManapiString.hpp"
#include "std/ManapiChain.hpp"
#include "ManapiMath.hpp"
#include "ManapiErrors.hpp"
#include "ManapiInitTools.hpp"

#include <deque>

#include "./utest.h"
#include "tools.hpp"

UTEST(std_string, str_split_1) {
    std::string c = "hello12world12two1";
    auto z = manapi::string::split (c, "12");
    ASSERT_TRUE( z.size() == 3 );
    ASSERT_TRUE( z[0] == "hello" && z[1] == "world" && z[2] == "two1");
}

UTEST(std_string, str_split_2) {
    std::string c = "test1test2test3test4";
    auto z = manapi::string::split (c, "test", 2);
    ASSERT_TRUE( z.size() == 3 );
    ASSERT_TRUE( z[0] == "" && z[1] == "1" && z[2] == "2test3test4");
}

UTEST(std_string, str_split_3) {
    std::string c = "test1test2test3test4";
    auto z = manapi::string::split (c, '1');
    ASSERT_TRUE( z.size() == 2 );
    ASSERT_TRUE( z[0] == "test" && z[1] == "test2test3test4");
}

UTEST(std_string, str_split_4) {
    std::string c = "hello";
    auto z = manapi::string::split (c, "");
    ASSERT_TRUE( z.size() == c.size() );
    ASSERT_TRUE( z[0] == "h" && z[1] == "e" && z[4] == "o");
}

UTEST(std_string, str_replace_1) {
    std::string a = "Hello$2!";
    auto res = manapi::string::replace(a, "$2", ", world");

    ASSERT_TRUE(a == "Hello, world!");
    ASSERT_TRUE(1 == res);
}

UTEST(std_string, str_replace_dup) {
    std::string a = "/path/to/file";
    auto res = manapi::string::replace(a, "/", "/");

    ASSERT_TRUE(a == "/path/to/file");
    ASSERT_TRUE(3 == res);
}

UTEST(std_string, str_replace_dup2) {
    std::string a = "/path/to/file";
    auto res = manapi::string::replace(a, "/", "//");

    ASSERT_TRUE(a == "//path//to//file");
    ASSERT_TRUE(3 == res);
}

UTEST(std_string, str_replace_2) {
    std::string a = "Hello$2!Hello$2!";
    auto res = manapi::string::replace(a, "$2", ", world");

    ASSERT_TRUE(a == "Hello, world!Hello, world!");
    ASSERT_TRUE(2 == res);
}

UTEST(std_string, str_replace_3) {
    std::string a = "Hello$2!Hello$2!";
    auto res = manapi::string::replace(a, "$2", ", world", 1);

    ASSERT_TRUE(a == "Hello, world!Hello$2!");
    ASSERT_TRUE(1 == res);
}

UTEST(std_string, str_replace_4) {
    std::string a = "Hello$2!Hello$2!";
    auto res = manapi::string::replace(a, "$2", ", world", 0);

    ASSERT_TRUE(a == "Hello$2!Hello$2!");
    ASSERT_TRUE(0 == res);
}


UTEST(std_string, str_replace_5) {
    std::string a = "$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2";
    auto res = manapi::string::replace(a, "$2", ", world", 9);

    ASSERT_TRUE(a == ", world, world, world, world, world, world, world, world, world$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2");
    ASSERT_TRUE(9 == res);
}

UTEST(std_string, str_replace_7) {
    std::string a = "Hello World!\nIt is a good time for it\n";
    auto res = manapi::string::replace(a, "It is a good time for it", "");

    ASSERT_TRUE(a == "Hello World!\n\n");
}

UTEST(std_string, chain_1) {
    manapi::chain<int> a;
    a.push_back(1);
    a.push_back(2);
    ASSERT_TRUE(a.size() == 2);
    auto a1 = a.back();
    a.pop_back();
    ASSERT_TRUE(a1 == 2);
    auto a2 = a.front();
    a.pop_front();
    ASSERT_TRUE(a2 == 1);
}

UTEST(std_string, chain_2) {
    manapi::chain<int> a;
    a.push_back(1);
    a.push_back(2);
    ASSERT_TRUE(a.size() == 2);
    auto a2 = a.front();
    a.pop_front();
    ASSERT_TRUE(a2 == 1);
    auto a1 = a.back();
    a.pop_back();
    ASSERT_TRUE(a1 == 2);
}

UTEST(std_string, chain_3) {
    manapi::chain<int> a;
    std::deque<int> b;

    for (int i =0 ; i < 500; i++) {
        int action = (int)manapi::math::random(0, 8);
        int s = (int)manapi::math::random(0, 10000);
        ASSERT_TRUE(a.empty() == b.empty());
        if (a.empty() || (action % 2 == 0 && action <= 6)) {
            b.push_back(s);
            a.push_back(s);
        }
        else if (action % 2 == 1 && action <= 6) {
            b.push_front(s);
            a.push_front(s);
        }
        else if (action % 2==0) {
            b.pop_back();
            a.pop_back();
        }
        else if (action % 2 == 1) {
            b.pop_front();
            a.pop_front();
        }
    }

    ASSERT_TRUE(b.size() == a.size());

    int cnt=0;

    auto bit = b.begin();
    auto ait = a.begin();

    for (; ait != a.end() && bit != b.end(); ait++, bit++) {
        ASSERT_TRUE(*ait == *bit);
        cnt++;
    }

    ASSERT_TRUE(cnt == a.size());
}

UTEST(std_string, chain_4) {
    manapi::chain<int> a;
    std::vector<int> values;
    std::vector<decltype(a)::chain_iterator> its;

    for (int i =0 ; i < 500; i++) {
        int s = (int)manapi::math::random(0, 100000);
        a.push_back(s);
        its.push_back(a.rbegin());
        values.push_back(s);
    }

    for (int i = 0; i < 500; i++) {
        int p = (int)manapi::math::random(0, a.size() - 1);
        ASSERT_TRUE(*its[p] == values[p]);
        a.erase(its[p]);
        values.erase(values.begin() + p);
        its.erase(its.begin() + p);
    }
}

UTEST(std_exception, exception_fmt) {
    try {
        throw manapi::exception (manapi::ERR_INTERNAL, "YOU ARE %s", "LUCKY");
    }
    catch (manapi::exception const &e) {
        ASSERT_TRUE(std::string_view{e.what()} == "YOU ARE LUCKY");
    }
}

UTEST(std_mutex, mutex_cancel) {
    auto ctx = init_ctx(utest_result, 5000);

    bool finished = false;

    manapi::async::run([&finished] () -> manapi::future<> {
        manapi::async::mutex mx;
        auto lk = co_await mx.lock_guard();
        finished = !(co_await mx.lock(manapi::ctokens::timeout(20)));
    });

    manapi::async::current()->timerpool()->append_timer_sync(500,
        manapi::TIMER_DEFAULT, [&ctx] (const manapi::timer &t) -> void {
        manapi::async::run(ctx->stop());
    });


    wait_ctx(ctx);

    ASSERT_TRUE(finished == true);
}

UTEST(std_mutex, mutex_cancel2) {
    auto ctx = init_ctx(utest_result, 5000);

    bool finished = false;

    manapi::async::run([&finished] () -> manapi::future<> {
        manapi::async::mutex mx;
        auto lk = co_await mx.lock_guard();
        auto res = (co_await mx.lock_guard(manapi::ctokens::timeout(20)));
        finished = !res;
    });

    manapi::async::current()->timerpool()->append_timer_sync(500,
        manapi::TIMER_DEFAULT, [&ctx] (const manapi::timer &t) -> void {
        manapi::async::run(ctx->stop());
    });


    wait_ctx(ctx);

    ASSERT_TRUE(finished == true);
}

UTEST(std_mutex, tmutex_cancel) {
    auto ctx = init_ctx(utest_result, 5000);

    bool finished = false;

    manapi::async::run([&finished] () -> manapi::future<> {
        manapi::async::tmutex mx;
        auto lk = co_await mx.lock_guard();
        finished = !(co_await mx.lock(manapi::ctokens::timeout(20)));
        manapi::async::run(manapi::async::current()->stop());
    });

    manapi::async::current()->timerpool()->append_timer_sync(500,
        manapi::TIMER_DEFAULT, [] (const manapi::timer &t) -> void {
        manapi::async::run(manapi::async::current()->stop());
    });


    wait_ctx(ctx);

    ASSERT_TRUE(finished == true);
}

UTEST(std_mutex, tmutex_cancel2) {
    auto ctx = init_ctx(utest_result, 5000);

    bool finished = false;

    manapi::async::run([&finished] () -> manapi::future<> {
        manapi::async::tmutex mx;
        auto lk = co_await mx.lock_guard();
        auto res = (co_await mx.lock_guard(manapi::ctokens::timeout(20)));
        finished = !res;
        manapi::async::run(manapi::async::current()->stop());
    });

    manapi::async::current()->timerpool()->append_timer_sync(500,
        manapi::TIMER_DEFAULT, [] (const manapi::timer &t) -> void {
        manapi::async::run(manapi::async::current()->stop());
    });


    wait_ctx(ctx);

    ASSERT_TRUE(finished == true);
}

UTEST(std_mutex, tmutex_cancel3) {
    auto ctx = init_ctx(utest_result, 5000, 15);

    std::atomic<int> finished{0};
    bool z = false;
    manapi::async::tmutex mx;
    ctx->run(15, [&finished, &z, &mx] (std::function<void()> bind) -> void {
        manapi::async::run([&finished, &z, &mx] () -> manapi::future<> {

            auto lk = co_await mx.lock_guard();
            if (z)
                finished.fetch_add(1);
            z = true;
            if (!(co_await mx.lock(manapi::ctokens::timeout(20))))
                finished.fetch_add(1);
            manapi::async::run(manapi::async::current()->stop());
            z = false;
        });

        manapi::async::current()->timerpool()->append_timer_sync(500,
            manapi::TIMER_DEFAULT, [] (const manapi::timer &t) -> void {
            manapi::async::run(manapi::async::current()->stop());
        });

        bind();
    });

    ASSERT_TRUE(finished == 16);
}

UTEST(std_mutex, tmutex_cancel4) {
    auto ctx = init_ctx(utest_result, 5000, 15);

    std::atomic<int> finished{0};
    bool z{false};
    manapi::async::tmutex mx;
    ctx->run(15, [&finished,&z,&mx] (std::function<void()> bind) -> void {
        manapi::async::run([&finished,&z,&mx] () -> manapi::future<> {
            auto lk = co_await mx.lock_guard();
            if (z)
                finished.fetch_add(1);
            z = true;
            auto res = co_await mx.lock_guard(manapi::ctokens::timeout(20));
            if (!(res.ok()))
                finished.fetch_add(1);
            manapi::async::run(manapi::async::current()->stop());
            z = false;
        });

        manapi::async::current()->timerpool()->append_timer_sync(500,
            manapi::TIMER_DEFAULT, [] (const manapi::timer &t) -> void {
            manapi::async::run(manapi::async::current()->stop());
        });

        bind();
    });

    ASSERT_TRUE(finished == 16);
}

UTEST_MAIN();