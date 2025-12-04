#include "ManapiString.hpp"

#include "./utest.h"
#include "./tools.hpp"

UTEST(string, replace_1) {
    std::string a = "Hello$2!";
    auto res = manapi::string::replace(a, "$2", ", world");

    ASSERT_TRUE(a == "Hello, world!");
    ASSERT_TRUE(1 == res);
}

UTEST(string, replace_2) {
    std::string a = "Hello$2!Hello$2!";
    auto res = manapi::string::replace(a, "$2", ", world");

    ASSERT_TRUE(a == "Hello, world!Hello, world!");
    ASSERT_TRUE(2 == res);
}

UTEST(string, replace_3) {
    std::string a = "Hello$2!Hello$2!";
    auto res = manapi::string::replace(a, "$2", ", world", 1);

    ASSERT_TRUE(a == "Hello, world!Hello$2!");
    ASSERT_TRUE(1 == res);
}

UTEST(string, replace_4) {
    std::string a = "Hello$2!Hello$2!";
    auto res = manapi::string::replace(a, "$2", ", world", 0);

    ASSERT_TRUE(a == "Hello$2!Hello$2!");
    ASSERT_TRUE(0 == res);
}


UTEST(string, replace_5) {
    std::string a = "$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2";
    auto res = manapi::string::replace(a, "$2", ", world", 9);

    ASSERT_TRUE(a == ", world, world, world, world, world, world, world, world, world$2$2$2$2$2$2$2$2$2$2$2$2$2$2$2");
    ASSERT_TRUE(9 == res);
}

MANAPIHTTP_TESTS_MAIN