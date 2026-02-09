#include "std/ManapiSlice.hpp"
#include <vector>

#include "./utest.h"
#include "./tools.hpp"
#include "ManapiMath.hpp"
#include "crypto/ManapiAES.hpp"

static int rcmp (int a) {
    if (a == 0) return 0;
    if (a < 0) return -1;
    return 1;
}

UTEST(slice, slice_push) {
    auto ctx = init_ctx(utest_result);
    {
        std::vector<std::string> c {"test #1", "test #2", "test #3"};
        manapi::slice b;
        std::string s;
        for (int i = 0; i < c.size(); i++) {
            b.push_back(c[i].data(), c[i].size());
            s.append(c[i]);
        }
        ASSERT_TRUE(!b.cmp(s.data(), s.size()));
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_pop) {
    auto ctx = init_ctx(utest_result);
    {
        std::vector<std::string> c {"test #1", "test #2", "test #3"};
        manapi::slice b;
        std::string s;
        for (int i = 0; i < c.size(); i++) {
            b.push_back(c[i].data(), c[i].size());
            if (i != c.size() - 1)
                s.append(c[i]);
        }
        b.resize(b.size() - c[2].size());
        ASSERT_TRUE(!b.cmp(s.data(), s.size()));
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}


UTEST(slice, slice_cmp1) {
    auto ctx = init_ctx(utest_result);
    {
        std::vector<std::string> c {"aaaa"};
        manapi::slice b;
        std::string s = "aaa";
        for (int i = 0; i < c.size(); i++) {
            b.push_back(c[i].data(), c[i].size());
        }
        ASSERT_TRUE(0 < b.cmp(s.data(), s.size()));
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_cmp2) {
    auto ctx = init_ctx(utest_result);
    {
        std::vector<std::string> c {"aaaa"};
        manapi::slice b;
        std::string s = "aaaa";
        for (int i = 0; i < c.size(); i++) {
            b.push_back(c[i].data(), c[i].size());
        }
        ASSERT_TRUE(0 == b.cmp(s.data(), s.size()));
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_cmp3) {
    auto ctx = init_ctx(utest_result);
    {
        std::vector<std::string> c {"aab"};
        manapi::slice b;
        std::string s = "aaa";
        for (int i = 0; i < c.size(); i++) {
            b.push_back(c[i].data(), c[i].size());
        }
        ASSERT_TRUE(0 < b.cmp(s.data(), s.size()));
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}


UTEST(slice, slice_cmp4) {
    auto ctx = init_ctx(utest_result);
    {
        std::vector<std::string> c {"aab"};
        manapi::slice b;
        std::string s = "aca";
        for (int i = 0; i < c.size(); i++) {
            b.push_back(c[i].data(), c[i].size());
        }
        ASSERT_TRUE(0 > b.cmp(s.data(), s.size()));
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_cmp5) {
    auto ctx = init_ctx(utest_result);
    {
        std::vector<std::string> c {"aaa"};
        manapi::slice b;
        std::string s = "aaaa";
        for (int i = 0; i < c.size(); i++) {
            b.push_back(c[i].data(), c[i].size());
        }
        ASSERT_TRUE(0 > b.cmp(s.data(), s.size()));
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_cmp8) {
    auto ctx = init_ctx(utest_result);
    for (int i = 0; i < 20; i++) {
        auto s3 = manapi::crypto::random_string(65536 * 2 + 100).unwrap();
        std::string s1 = s3 + manapi::crypto::random_string(65536 * 8).unwrap();
        std::string s2 = s3 + manapi::crypto::random_string(65536 * 8).unwrap();
        auto ra1 = manapi::math::random(0, s2.size() - 1);
        auto ra2 = manapi::math::random(0, ra1);
        auto rd1 = manapi::math::random(0, s1.size() - 1);
        auto rd2 = manapi::math::random(0, rd1);
        manapi::slice b1;
        manapi::slice b2;
        b1.push_back(s1.data(), s1.size());
        b2.push_back(s2.data(), s2.size());
        s1 = s1.substr(ra2, ra1 - ra2);
        s2 = s2.substr(rd2, rd1 - rd2);
        auto bb1 = b1.subslice(ra2, ra1 - ra2).unwrap();
        auto bb2 = b2.subslice(rd2, rd1 - rd2).unwrap();
        int l1 = rcmp(memcmp(s1.data(), s2.data(), s1.size()));
        int l2 = rcmp(bb1.cmp(bb2));
        ASSERT_TRUE(l1 == l2);
        if (l1 == 1)
            ASSERT_TRUE(s1 > s2);
        else if (l2 == -1)
            ASSERT_TRUE(s1 < s2);
        else
            ASSERT_TRUE(s1 == s2);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}


UTEST(slice, slice_cmp9) {
    auto ctx = init_ctx(utest_result);
    for (int i = 0; i < 20; i++) {
        auto s3 = manapi::crypto::random_string(65536 * 2 + 100).unwrap();
        std::string s1 = s3 + manapi::crypto::random_string(65536 * 8).unwrap();
        std::string s2 = s3 + manapi::crypto::random_string(65536 * 8).unwrap();
        auto rd = manapi::math::random(0, s2.size());
        manapi::slice b1;
        manapi::slice b2;
        b1.push_back(s1.data(), s1.size());
        b2.push_back(s2.data(), s2.size());
        s2.resize(rd);
        int l1 = rcmp(memcmp(s1.data(), s2.data(), s1.size()));
        int l2 = rcmp(b1.cmp(b2.subslice(0, rd).unwrap()));
        ASSERT_TRUE(l1 == l2);
        if (l1 == 1)
            ASSERT_TRUE(s1 > s2);
        else if (l2 == -1)
            ASSERT_TRUE(s1 < s2);
        else
            ASSERT_TRUE(s1 == s2);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_cmp6) {
    auto ctx = init_ctx(utest_result);
    for (int i = 0; i < 50; i++) {
        auto s3 = manapi::crypto::random_string(65536 * 2 + 100).unwrap();
        std::string s1 = s3 + manapi::crypto::random_string(65536 * 8).unwrap();
        std::string s2 = s3 + manapi::crypto::random_string(65536 * 8).unwrap();
        manapi::slice b;
        b.push_back(s1.data(), s1.size());
        int l1 = rcmp(memcmp(s1.data(), s2.data(), s1.size()));
        int l2 = rcmp(b.cmp(s2.data(), s2.size()));
        ASSERT_TRUE(l1 == l2);
        if (l1 == 1)
            ASSERT_TRUE(s1 > s2);
        else if (l2 == -1)
            ASSERT_TRUE(s1 < s2);
        else
            ASSERT_TRUE(s1 == s2);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_cmp7) {
    auto ctx = init_ctx(utest_result);
    for (int i = 0; i < 50; i++) {
        auto s3 = manapi::crypto::random_string(65536 * 2 + 100).unwrap();
        std::string s1 = s3 + manapi::crypto::random_string(65536 * 8).unwrap();
        std::string s2 = s3 + manapi::crypto::random_string(65536 * 8).unwrap();
        manapi::slice b1;
        manapi::slice b2;
        b1.push_back(s1.data(), s1.size());
        b2.push_back(s2.data(), s2.size());
        int l1 = rcmp(memcmp(s1.data(), s2.data(), s1.size()));
        int l2 = rcmp(b1.cmp(b2));
        ASSERT_TRUE(l1 == l2);
        if (l1 == 1)
            ASSERT_TRUE(s1 > s2);
        else if (l2 == -1)
            ASSERT_TRUE(s1 < s2);
        else
            ASSERT_TRUE(s1 == s2);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

MANAPIHTTP_TESTS_MAIN