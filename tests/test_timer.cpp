#include <std/ManapiTimer.hpp>

#include "ManapiInitTools.hpp"
#include "ManapiProcess.hpp"
#include "json/ManapiJson.hpp"
#include "json/ManapiJsonMask.hpp"
#include "json/ManapiJsonBuilder.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "ManapiTimerPool.hpp"

#include "./utest.h"
#include "./tools.hpp"

UTEST(timer, important_dep) {
    auto ctx = init_ctx(utest_result, 5000);

    bool finished = false;

    manapi::async::current()->timerpool()->append_timer_sync(20,
        manapi::TIMER_IMPORTANT, [&finished] (const manapi::timer &t) -> void {
        finished = true;
    });

    send_stop();

    wait_ctx(ctx);

    ASSERT_TRUE(finished == true);
}

UTEST(timer, default_dep) {
    auto ctx = init_ctx(utest_result, 10000);

    bool finished = false;

    manapi::async::current()->timerpool()->append_timer_sync(5000,
        manapi::TIMER_DEFAULT, [&finished] (const manapi::timer &t) -> void {
        finished = true;
    });

    send_stop();

    wait_ctx(ctx);

    ASSERT_TRUE(finished == false);
}

UTEST(timer, interval) {
    auto ctx = init_ctx(utest_result, 5000);

    int a = 0;

    manapi::async::current()->timerpool()->append_interval_sync(20,
        manapi::TIMER_IMPORTANT, [&a] (manapi::timer t) -> void {
        a++;
        if (a >= 5) {
            t.stop();
        }
    });

    send_stop();

    wait_ctx(ctx);

    ASSERT_TRUE(a == 5);
}

UTEST(timer, interval_async) {
    auto ctx = init_ctx(utest_result, 5000);

    int a = 0;

    manapi::async::current()->timerpool()->append_interval_async(20,
        manapi::TIMER_IMPORTANT, [&a] (manapi::timer t) -> manapi::future<> {
        a++;
        co_await manapi::async::delay{30};
        if (a >= 5) {
            t.stop();
        }
    });
    send_stop();

    wait_ctx(ctx);

    ASSERT_TRUE(a == 5);
}

UTEST(timer, async) {
    auto ctx = init_ctx(utest_result, 5000);

    int a = 0;

    manapi::async::current()->timerpool()->append_timer_async(20,
        manapi::TIMER_IMPORTANT, [&a] (manapi::timer t) -> manapi::future<> {
        co_await manapi::async::delay{20};
        a = 5;
    });

    send_stop();
    wait_ctx(ctx);

    ASSERT_TRUE(a == 5);
}

UTEST(timer, again) {
    auto ctx = init_ctx(utest_result, 5000);

    int a = 0;

    manapi::async::current()->timerpool()->append_timer_async(20,
        manapi::TIMER_IMPORTANT, [&a] (manapi::timer t) -> manapi::future<> {
        co_await manapi::async::delay{20};
        a ++;
        if (a < 5) {
            t.again(20);
        }
    });
    send_stop();

    wait_ctx(ctx);

    ASSERT_TRUE(a == 5);
}

UTEST(timer, interval_again) {
    auto ctx = init_ctx(utest_result, 5000);

    int a = 0;

    manapi::async::current()->timerpool()->append_interval_sync(20,
        manapi::TIMER_IMPORTANT, [&a] (manapi::timer t) -> void {
        a ++;
        if (a < 5) {
            t.again(20);
        }
        else {
            t.stop();
        }
    });
    send_stop();

    wait_ctx(ctx);

    ASSERT_TRUE(a == 5);
}

MANAPIHTTP_TESTS_MAIN