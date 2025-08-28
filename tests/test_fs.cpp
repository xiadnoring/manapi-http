#   include "ManapiInitTools.hpp"
#   include "ManapiProcess.hpp"
#   include "json/ManapiJson.hpp"
#   include "json/ManapiJsonMask.hpp"
#   include "json/ManapiJsonBuilder.hpp"
#   include "fs/ManapiFilesystem.hpp"
#include "./utest.h"

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
    std::string path = manapi::filesystem::path::join ("./hello", ".", "test", "..", ".", "no");
    std::string s = manapi::filesystem::path::current_path();
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("hello");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("no");
    ASSERT_TRUE (path == (s));
}

UTEST(fs_path, serialize_1) {
    std::string path = manapi::filesystem::path::serialize("/////dev//./././shm//shm/../../../dev//shm//././/dev/shm/../..");
    std::string s = "";
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("dev");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("shm");
    ASSERT_TRUE (path == (s));
}

UTEST(fs_path, serialize_2) {
    std::string path = manapi::filesystem::path::serialize("./////dev//./././shm//shm/../../../dev//shm//././/dev/shm/../..");
    std::string s = manapi::filesystem::path::current_path();
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("dev");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("shm");
    ASSERT_TRUE (path == (s));
}


UTEST(fs_path, serialize_3) {
    std::string path = manapi::filesystem::path::serialize("hello/next");
    std::string s = {};
    s.append("hello");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("next");
    ASSERT_TRUE (path == (s));
}

UTEST(fs_path, serialize_4) {
    std::string path = manapi::filesystem::path::serialize("/hello/next");
    std::string s = {};
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("hello");
    s.append(manapi::filesystem::path::string_delimiter);
    s.append("next");
    ASSERT_TRUE (path == (s));
}

UTEST_STATE();

int main(int argc, const char *const argv[]) {

    try {
        manapi::init_tools::log_trace_init((manapi::debug::trace_level)std::stoi(manapi::process::get_env("MANAPIHTTP_LOGTRACE").unwrap()));
    }
    catch (...) {
        manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_NONE);
    }

    manapi::async::context::threadpoolfs(2);
    manapi::async::context::gbs = manapi::async::context::blockedsignals();

    return utest_main(argc, argv);
}