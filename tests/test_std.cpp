#include "ManapiString.hpp"
#include "std/ManapiChain.hpp"
#include "ManapiMath.hpp"
#include "ManapiErrors.hpp"

#include <deque>

#include "./utest.h"

UTEST(std_string, str_replace_1) {
    std::string a = "Hello$2!";
    auto res = manapi::string::replace(a, "$2", ", world");

    ASSERT_TRUE(a == "Hello, world!");
    ASSERT_TRUE(1 == res);
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
        int action = manapi::math::random(0, 8);
        int s = manapi::math::random(0, 10000);
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
        int s = manapi::math::random(0, 100000);
        a.push_back(s);
        its.push_back(a.rbegin());
        values.push_back(s);
    }

    for (int i = 0; i < 500; i++) {
        int p = manapi::math::random(0, a.size() - 1);
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

UTEST_MAIN();