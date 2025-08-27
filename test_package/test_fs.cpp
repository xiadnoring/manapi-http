#include "test_fs.hpp"

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