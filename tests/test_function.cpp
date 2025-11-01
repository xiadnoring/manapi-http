#include <utility>

#include "std/ManapiFunction.hpp"

#include "./utest.h"
#include "./tools.hpp"

UTEST(func, function_call) {
    manapi::fixed_function<int()> cb = [] () -> int {
        return 1;
    };

    ASSERT_TRUE(cb() == 1);
}

UTEST(func, function_data) {
    int a = 5;
    manapi::fixed_function<int()> cb = [a] () -> int {
        return a;
    };

    ASSERT_TRUE(cb() == a);
}

UTEST(func, function_move) {
    int a = 5;
    manapi::fixed_function<int()> cb1 = [a] () mutable -> int {
        return std::exchange(a, 7);
    };
    manapi::fixed_function<int()> cb2 = [a] () mutable -> int {
        return a;
    };
    ASSERT_TRUE(cb1 () == a);
    cb2 = std::move(cb1);
    ASSERT_TRUE(cb2 () == 7);
}

UTEST(func, function_assign) {
    manapi::fixed_function<int()> cb1 = [] () -> int {
        return 5;
    };
    manapi::fixed_function<int(manapi::fixed_function<int()>)> cb2 = [] (manapi::fixed_function<int()> b) mutable -> int {
        return b ();
    };
    ASSERT_TRUE(cb1 () == 5);
    ASSERT_TRUE(cb2 (std::move(cb1)) == 5);
}

UTEST(func, function_static) {
    int c1 = 1;
    int c2 = 2;
    int c3 = 3;

    auto cb1 = manapi::static_function<int, int>([c1,c2,c3] (int a) -> int {
        a += c1 + c2 + c3;
        return a;
    });

    ASSERT_TRUE(cb1 (5) == 5 + c1 + c2 + c3);

    auto cb2 = manapi::static_function<long long, int>([c1,c2,c3] (int a) -> long long {
        a += c1 + c2 + c3;
        return a;
    });

    ASSERT_TRUE(cb2 (5) == 5 + c1 + c2 + c3);
}

MANAPIHTTP_TESTS_MAIN