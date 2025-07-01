#pragma once

#ifdef MANAPIHTTP_HTTP_AS_EXECUTABLE
#   include "ManapiBigint.hpp"
#else
#   include <manapihttp/ManapiBigint.hpp>
#endif
#include "./utest.h"

UTEST(bigint, bigint_integer) {
    manapi::bigint n (78);
    ASSERT_TRUE(n == 78);
}

UTEST(bigint, bigint_signed_integer) {
    manapi::bigint n (-78);
    ASSERT_TRUE(n == -78);
}

UTEST(bigint, bigint_decimal) {
    manapi::bigint n (17.89);
    ASSERT_TRUE(n == 17.89);
}

UTEST(bigint, bigint_signed_decimal) {
    manapi::bigint n (-17.89);
    ASSERT_TRUE(n == -17.89);
}

UTEST(bigint, bigint_parse_integer) {
    manapi::bigint n ("78");
    ASSERT_TRUE(n == 78);
}

UTEST(bigint, bigint_parse_signed_integer) {
    manapi::bigint n ("-78");
    ASSERT_TRUE(n == -78);
}

UTEST(bigint, bigint_parse_decimal) {
    manapi::bigint n ("78.78");
    ASSERT_TRUE(n.stringify() == "78.78");
}

UTEST(bigint, bigint_parse_signed_decimal) {
    manapi::bigint n ("-78.78");
    ASSERT_TRUE(n.stringify() == "-78.78");
}

UTEST(bigint, bigint_parse_exp_decimal) {
    manapi::bigint n ("1e8");
    ASSERT_TRUE(n == 1e8);
}

UTEST(bigint, bigint_parse_large_integer) {
    manapi::bigint n ("12345678901234567890123456789012345678901234567890123456789012345678901234567890", 1024);
    ASSERT_TRUE(n.stringify() == "12345678901234567890123456789012345678901234567890123456789012345678901234567890");
}

UTEST(bigint, bigint_parse_sum_integer) {
    manapi::bigint n1 ("12345678901234567890123456789012345678901234567890123456789012345678901234567890", 1024);
    manapi::bigint n2 ("3445645684506984506984506948650948650495860495864506456", 1024);
    auto n3 = n1 + n2;
    ASSERT_TRUE(n3.stringify() == "12345678901234567890123460234658030185885741552397072107737662841539397099074346");
}

UTEST(bigint, bigint_parse_extract_integer) {
    manapi::bigint n1 ("128", 1024);
    manapi::bigint n2 ("28", 1024);
    auto n3 = n1 - n2;
    ASSERT_TRUE(n3.stringify() == "100");
}

UTEST(bigint, bigint_parse_increase_integer) {
    manapi::bigint n1 ("78", 1024);
    manapi::bigint n2 ("2", 1024);
    auto n3 = n1 * n2;
    ASSERT_TRUE(n3.integerify() == 78 * 2);
}

UTEST(bigint, bigint_parse_divide_integer) {
    manapi::bigint n1 ("78", 1024);
    manapi::bigint n2 ("2", 1024);
    auto n3 = n1 / n2;
    ASSERT_TRUE(n3.integerify() == 78 / 2);
}