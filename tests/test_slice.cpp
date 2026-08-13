#include <vector>

#include "ManapiInitTools.hpp"
#include "ManapiProcess.hpp"
#include "ManapiFetch2.hpp"
#include "ManapiString.hpp"
#include "ManapiHttp.hpp"
#include "json/ManapiJson.hpp"
#include "std/ManapiEasyCancellation.hpp"

#include "ManapiMath.hpp"
#include "crypto/ManapiAES.hpp"
#include "std/ManapiSlice.hpp"

#include "./utest.h"
#include "./tools.hpp"

static int rcmp (int a) {
    if (a == 0) return 0;
    if (a < 0) return -1;
    return 1;
}

UTEST(slice, slice_concat_small) {
    auto ctx = init_ctx(utest_result);
    {
        manapi::slice sv;
        auto rz1 = manapi::string::random(10);
        auto rz2 = manapi::string::random(20);
        auto g1 = manapi::async::memory_fabric()->slice(manapi::object_pool::area_size()).unwrap();
        auto g2 = manapi::async::memory_fabric()->slice(manapi::object_pool::area_size()).unwrap();

        g1.copy_from(rz1.data(), 0, rz1.size()).unwrap();
        g2.copy_from(rz2.data(), 0, rz2.size()).unwrap();

        g1.resize(rz1.size()).unwrap();
        g2.resize(rz2.size()).unwrap();

        sv.push_back(std::move(g1)).unwrap();
        sv.push_back(std::move(g2)).unwrap();

        std::string rz = rz1 + rz2;

        ASSERT_TRUE( sv.cmp(rz.data(), rz.size()) == 0 );
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_buf_1) {
    auto ctx = init_ctx(utest_result);
    {
        auto b = manapi::async::memory_fabric()->buffer(4096).unwrap();
        ASSERT_TRUE(b.size() == 4096);
        auto rz = manapi::string::random(4096);

        ::memcpy (b.data(), rz.data(), rz.size());

        b.resize(8096);

        ASSERT_TRUE(b.size() == 8096);

        ASSERT_TRUE(::memcmp( b.data(), rz.data(), rz.size() ) == 0);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_buf_2) {
    auto ctx = init_ctx(utest_result);
    {
        auto b = manapi::async::memory_fabric()->buffer(0).unwrap();
        ASSERT_TRUE(b.size() == 0);
        auto rz = manapi::string::random(2000);

        b.resize(2000).unwrap();

        ASSERT_TRUE(b.size() == 2000);

        ::memcpy (b.data(), rz.data(), rz.size());

        ASSERT_TRUE(::memcmp( b.data(), rz.data(), rz.size() ) == 0);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_buf_3) {
    auto ctx = init_ctx(utest_result);
    {
        auto b = manapi::async::memory_fabric()->buffer(4000).unwrap();
        ASSERT_TRUE(b.size() == 4000);
        auto rz = manapi::string::random(200);

        ::memcpy (b.data(), rz.data(), rz.size());

        b.resize(4090);

        ASSERT_TRUE(b.size() == 4090);

        ASSERT_TRUE(::memcmp( b.data(), rz.data(), rz.size() ) == 0);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
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
        b.resize(b.size() - c[2].size()).unwrap();
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
        ASSERT_TRUE(s1.size() == s2.size());
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
        ASSERT_TRUE(!bb1.cmp(s1.data(), s1.size()));
        ASSERT_TRUE(!bb2.cmp(s2.data(), s2.size()));
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
        s1.resize(rd);
        int l1 = rcmp(memcmp(s1.data(), s2.data(), rd));
        int l2 = rcmp(b1.subslice(0, rd).unwrap().cmp(b2.subslice(0, rd).unwrap()));
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

UTEST(slice, slice_cmp10) {
    auto ctx = init_ctx(utest_result);
    for (int i = 0; i < 1; i++) {
        auto s1 = manapi::crypto::random_string(10000).unwrap();
        manapi::slice b;
        b.push_back(s1.data(), s1.size());
        auto res = b.subslice(9999).unwrap();
        ASSERT_TRUE(res.size() == 1 && !res.cmp(s1.data() + 9999, 1));
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

#define SLICE_CHECK(__z_sv) {std::size_t __z_z_cnt = 0; for (auto _ : (__z_sv)) __z_z_cnt++; ASSERT_EQ(__z_z_cnt, (__z_sv).slices_size());} \
    { std::size_t __z_total_size = 0; for (auto __z_b : (__z_sv)) __z_total_size += __z_b.size(); ASSERT_EQ(__z_total_size, (__z_sv).size()); }

UTEST(slice, slice_subslice) {
    auto ctx = init_ctx(utest_result);
    for (int i = 0; i < 50; i++) {
        auto s1 = manapi::crypto::random_string(10000).unwrap();
        manapi::slice b;
        b.push_back(s1.data(), s1.size());
        std::size_t z1 = manapi::math::random(1000, 8000);
        std::size_t z2 = manapi::math::random(z1, 9900) - z1;
        auto res = b.subslice(z1, z2).unwrap();
        ASSERT_TRUE(res.size() == z2);
        ASSERT_TRUE(!res.cmp(s1.data() + z1, z2));

        SLICE_CHECK(b);
        SLICE_CHECK(res);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}


UTEST(slice, slice_split) {
    auto ctx = init_ctx(utest_result);
    {
        auto s1 = manapi::crypto::random_string(10000).unwrap();
        manapi::slice b;
        b.push_back(s1.data(), s1.size());
        std::size_t z1 = 0;
        std::size_t z2 = 10000;
        auto res = b.split(z1, z2).unwrap();
        ASSERT_TRUE(res.size() == z2);
        ASSERT_TRUE(!res.cmp(s1.data() + z1, z2));
        std::string bz;
        bz.append(s1.data(), z1);
        bz.append(s1.data() + z1 + z2, s1.size() - z1 - z2);
        ASSERT_TRUE(b.size() == bz.size());
        ASSERT_TRUE(!b.cmp(bz.data(), bz.size()));

        SLICE_CHECK(b);
        SLICE_CHECK(res);
    }
    {
        auto s1 = manapi::crypto::random_string(10000).unwrap();
        manapi::slice b;
        b.push_back(s1.data(), s1.size());
        std::size_t z1 = 0;
        std::size_t z2 = 100;
        auto res = b.split(z1, z2).unwrap();
        ASSERT_TRUE(res.size() == z2);
        ASSERT_TRUE(!res.cmp(s1.data() + z1, z2));
        std::string bz;
        bz.append(s1.data(), z1);
        bz.append(s1.data() + z1 + z2, s1.size() - z1 - z2);
        ASSERT_TRUE(b.size() == bz.size() && !b.cmp(bz.data(), bz.size()));

        SLICE_CHECK(b);
        SLICE_CHECK(res);
    }
    {
        auto s1 = manapi::crypto::random_string(10000).unwrap();
        manapi::slice b;
        b.push_back(s1.data(), s1.size());
        std::size_t z1 = 10000;
        std::size_t z2 = 0;
        auto res = b.split(z1, z2).unwrap();
        ASSERT_TRUE(res.size() == z2);
        ASSERT_TRUE(!res.cmp(s1.data() + z1, z2));
        std::string bz;
        bz.append(s1.data(), z1);
        bz.append(s1.data() + z1 + z2, s1.size() - z1 - z2);
        ASSERT_TRUE(b.size() == bz.size() && !b.cmp(bz.data(), bz.size()));

        SLICE_CHECK(b);
        SLICE_CHECK(res);
    }
    {
        auto s1 = manapi::crypto::random_string(10000).unwrap();
        manapi::slice b;
        b.push_back(s1.data(), s1.size());
        std::size_t z1 = 4096;
        std::size_t z2 = 4096;
        auto res = b.split(z1, z2).unwrap();
        ASSERT_TRUE(res.size() == z2);
        ASSERT_TRUE(!res.cmp(s1.data() + z1, z2));
        std::string bz;
        bz.append(s1.data(), z1);
        bz.append(s1.data() + z1 + z2, s1.size() - z1 - z2);
        ASSERT_TRUE(b.size() == bz.size());
        ASSERT_TRUE(!b.cmp(bz.data(), bz.size()));

        SLICE_CHECK(b);
        SLICE_CHECK(res);
    }
    for (int i = 0; i < 50; i++) {
        auto s1 = manapi::crypto::random_string(10000).unwrap();
        manapi::slice b;
        b.push_back(s1.data(), s1.size());
        std::size_t z1 = manapi::math::random(0, 8000);
        std::size_t z2 = manapi::math::random(z1, 10000) - z1;
        auto res = b.split(z1, z2).unwrap();
        ASSERT_TRUE(res.size() == z2);
        ASSERT_TRUE(!res.cmp(s1.data() + z1, z2));
        std::string bz;
        bz.append(s1.data(), z1);
        bz.append(s1.data() + z1 + z2, s1.size() - z1 - z2);
        SLICE_CHECK(b);
        SLICE_CHECK(res);
        ASSERT_TRUE(b.size() == bz.size());
        ASSERT_TRUE(!b.cmp(bz.data(), bz.size()));

    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_subslice_range_err1) {
    auto ctx = init_ctx(utest_result);
    {
        manapi::slice b = manapi::async::memory_fabric()->slice(500).unwrap();
        ASSERT_TRUE(!b.subslice(0, 501).ok());
        ASSERT_TRUE(!b.subslice(1, 500).ok());
        ASSERT_TRUE(!b.subslice(2, 499).ok());
        ASSERT_TRUE(b.subslice(1, 499).ok());
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_subslice_range_err2) {
    auto ctx = init_ctx(utest_result);
    {
        manapi::slice b = manapi::async::memory_fabric()->slice(5000).unwrap();
        ASSERT_TRUE(!b.subslice(0, 5001).ok());
        ASSERT_TRUE(!b.subslice(1, 5000).ok());
        ASSERT_TRUE(!b.subslice(2, 4999).ok());
        ASSERT_TRUE(b.subslice(1, 4999).ok());
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}
UTEST(slice, slice_split_range_err1) {
    auto ctx = init_ctx(utest_result);
    {
        manapi::slice b = manapi::async::memory_fabric()->slice(500).unwrap();
        ASSERT_TRUE(!b.split(0, 501).ok());
        ASSERT_TRUE(!b.split(1, 500).ok());
        ASSERT_TRUE(!b.split(2, 499).ok());
        ASSERT_TRUE(b.split(1, 499).ok());
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_split_range_err2) {
    auto ctx = init_ctx(utest_result);
    {
        manapi::slice b = manapi::async::memory_fabric()->slice(5000).unwrap();
        ASSERT_TRUE(!b.split(0, 5001).ok());
        ASSERT_TRUE(!b.split(1, 5000).ok());
        ASSERT_TRUE(!b.split(2, 4999).ok());
        ASSERT_TRUE(b.split(1, 4999).ok());
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_subslice_range1_err1) {
    auto ctx = init_ctx(utest_result);
    {
        manapi::slice b = manapi::async::memory_fabric()->slice(500).unwrap();
        auto z4= (b.subslice(1, 499).unwrap());

        SLICE_CHECK(z4);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_subslice_range1_err2) {
    auto ctx = init_ctx(utest_result);
    {
        manapi::slice b = manapi::async::memory_fabric()->slice(5000).unwrap();
        auto z4=(b.subslice(1, 4999).unwrap());

        SLICE_CHECK(z4);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}
UTEST(slice, slice_split_range1_err1) {
    auto ctx = init_ctx(utest_result);
    {
        manapi::slice b = manapi::async::memory_fabric()->slice(500).unwrap();
        auto z4=(b.split(1, 499).unwrap());

        SLICE_CHECK(z4);
        SLICE_CHECK(b);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(slice, slice_split_range1_err2) {
    auto ctx = init_ctx(utest_result);
    {
        manapi::slice b = manapi::async::memory_fabric()->slice(5000).unwrap();
        auto z4=(b.split(1, 4999).unwrap());

        SLICE_CHECK(z4);
        SLICE_CHECK(b);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

MANAPIHTTP_TESTS_MAIN