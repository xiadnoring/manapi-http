//
// Created by Timur on 11/28/25.
//

#include "ManapiInitTools.hpp"
#include "ManapiProcess.hpp"
#include "json/ManapiJson.hpp"
#include "json/ManapiJsonMask.hpp"
#include "json/ManapiJsonBuilder.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "fs/ManapiFileStream.hpp"
#include "ManapiTimerPool.hpp"

#include "./utest.h"
#include "./tools.hpp"

UTEST(fs, unique_file) {
    auto ctx = init_ctx(utest_result);

    manapi::async::run ([&] () -> manapi::future<> {
        auto z = manapi::unwrap(co_await manapi::fs::async_open("create_test.tmp", manapi::ev::FS_O_CREAT|manapi::ev::FS_O_RDWR, 0755));

        auto handle = z.get();
#define return co_return
        ASSERT_TRUE(handle == z.release().unwrap());
#undef return
        z.reset (handle);

        z.reset();
        manapi::unwrap(co_await manapi::fs::async_unlink("create_test.tmp"));

        co_await ctx->stop();
    });

    wait_ctx(ctx);
}

MANAPIHTTP_TESTS_MAIN