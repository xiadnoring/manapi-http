#include "ManapiInitTools.hpp"
#include "ManapiProcess.hpp"
#include "json/ManapiJson.hpp"
#include "json/ManapiJsonMask.hpp"
#include "json/ManapiJsonBuilder.hpp"
#include "fs/ManapiFilesystem.hpp"
#include "ManapiTimerPool.hpp"

#include "./utest.h"
#include "./tools.hpp"

UTEST(fs_path, join_1) {
    std::string path = manapi::filesystem::path::join ("hello", "test", "no");
    std::string s{"hello"};
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("test");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("no");
    ASSERT_TRUE (path == (s));
}

UTEST(fs_path, join_2) {
    std::string path = manapi::filesystem::path::join ("hello", ".", "test", "..", ".", "no");
    std::string s{"hello"};
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("no");
    ASSERT_TRUE (path == (s));
}

UTEST(fs_path, join_3) {
    std::string path = manapi::filesystem::path::join ("hello", ".",".",".",".",".",".",".", "test", "..","test", "..","..","hello","test", "..", ".", "no");
    std::string s{"hello"};
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("no");
    ASSERT_TRUE (path == (s));
}

UTEST(fs_path, join_4) {
#ifdef _WIN32
    std::string path = manapi::filesystem::path::join (".\\hello", ".", "test", "..", ".", "no");
#else
    std::string path = manapi::filesystem::path::join ("./hello", ".", "test", "..", ".", "no");
#endif
    std::string s = manapi::filesystem::path::current_path();
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("hello");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("no");
    ASSERT_TRUE (path == (s));
}

UTEST(fs_path, join_5) {
    std::string path1 = manapi::filesystem::path::join(".", "..", "hello");
    std::string path2 = manapi::filesystem::path::join("..", "hello");
    ASSERT_TRUE (path1 == path2);
}

UTEST(fs_path, join_6) {
    std::string b = manapi::filesystem::path::root_directory();
    std::string path = manapi::filesystem::path::join (b, "hello", "test", "no");
    std::string s = b;
    s += std::string_view{"hello"};
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("test");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("no");
    ASSERT_TRUE (path == (s));
}

UTEST(fs_path, join_7) {
    std::string b = manapi::filesystem::path::root_directory();
    std::string path = manapi::filesystem::path::join ("A", "B", "C", "..", "..", "..", "..", "..", "..", ".", "A");
    ASSERT_TRUE (path == "A");
}

UTEST(fs_path, join_8) {
    std::string b = manapi::filesystem::path::root_directory();
    std::string path = manapi::filesystem::path::join ("A", "B", "C", "D");
    ASSERT_TRUE (manapi::string::count(manapi::filesystem::path::delimiter, path) == 3);
}

UTEST(fs_path, serialize_1) {
#ifdef _WIN32
    std::string path = manapi::filesystem::path::serialize("\\\\\\\\\\dev\\\\.\\.\\.\\shm\\\\shm\\..\\..\\..\\dev\\\\shm\\\\.\\.\\\\dev\\shm\\..\\..");
#else
    std::string path = manapi::filesystem::path::serialize("/////dev//./././shm//shm/../../../dev//shm//././/dev/shm/../..");
#endif
    std::string s = "";
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("dev");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("shm");
    ASSERT_TRUE (path == (s));
}

UTEST(fs_path, serialize_2) {
#ifdef _WIN32
    std::string path = manapi::filesystem::path::serialize(".\\\\\\\\\\dev\\\\.\\.\\.\\shm\\\\shm\\..\\..\\..\\dev\\\\shm\\\\.\\.\\\\dev\\shm\\..\\..");
#else
    std::string path = manapi::filesystem::path::serialize("./////dev//./././shm//shm/../../../dev//shm//././/dev/shm/../..");
#endif
    std::string s = manapi::filesystem::path::current_path();
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("dev");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("shm");
    ASSERT_TRUE (path == (s));
}


UTEST(fs_path, serialize_3) {
#ifdef _WIN32
    std::string path = manapi::filesystem::path::serialize("hello\\next");
#else
    std::string path = manapi::filesystem::path::serialize("hello/next");
#endif
    std::string s = {};
    s.append("hello");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("next");
    ASSERT_TRUE (path == (s));
}

UTEST(fs_path, serialize_4) {
#ifdef _WIN32
    std::string path = manapi::filesystem::path::serialize("\\hello\\next");
#else
    std::string path = manapi::filesystem::path::serialize("/hello/next");
#endif
    std::string s = {};
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("hello");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("next");
    ASSERT_TRUE (path == (s));
}

UTEST(fs, read_and_write_1) {
    auto ctx = init_ctx(utest_result);

    manapi::async::run ([&] () -> manapi::future<> {
        char test[100000];
        auto const path = manapi::filesystem::path::join(std::filesystem::temp_directory_path(), "data-test-read_and_write_1.txt");
        for (int i = 0; i < 100000; i++) {
            test[i] = (char)(i % 10);
        }
        auto exists_res = co_await manapi::filesystem::async_exists(path);
        if (exists_res.ok() && exists_res.unwrap()) {
            auto remove_res = co_await manapi::filesystem::async_unlink(path);
            remove_res.unwrap();
        }
        auto write_res = co_await manapi::filesystem::async_write (path, std::string (test, sizeof (test)), manapi::ev::IRWXU, manapi::ev::FS_O_CREAT|manapi::ev::FS_O_TRUNC|manapi::ev::FS_O_WRONLY);
        
#define return co_return
        ASSERT_TRUE_MSG (write_res.ok(), "check write result");
        auto read_res = co_await manapi::filesystem::async_read (path, manapi::ev::FS_O_RDONLY);
        ASSERT_TRUE_MSG (read_res.ok(), "check read result");
        auto result = read_res.unwrap();
        ASSERT_TRUE_MSG (result.size() == sizeof (test) && !memcmp (result.data(), test, sizeof (test)), "check fs read/write result");
#undef return

        co_await ctx->stop();
    });

    wait_ctx(ctx);
}

MANAPIHTTP_TESTS_MAIN