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
#include "compress/ManapiCompress.hpp"

#include "./utest.h"
#include "./tools.hpp"

UTEST(compress, compress_deflate) {
    auto ctx = init_ctx(utest_result);
    {
        manapi::slice z;
        for (int i = 0; i < 2; i++) {
            auto bt = manapi::async::memory_fabric()->buffer(manapi::object_pool::area_size()).unwrap();
            manapi::string::random(bt.data(), bt.size());

            z.push_back(std::move(bt));
        }

        manapi::slice ri;
        manapi::slice ro;

        {
            manapi::compress::deflate_compress dc (9);
            ri = dc.compress(z, true).unwrap();
        }

        {
            manapi::compress::deflate_decompress dd;
            ro = dd.decompress(ri, true).unwrap();
        }

        ASSERT_TRUE(z.cmp(ro) == 0);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(compress, compress_gzip) {
    auto ctx = init_ctx(utest_result);
    {
        manapi::slice z;
        for (int i = 0; i < 2; i++) {
            auto bt = manapi::async::memory_fabric()->buffer(manapi::object_pool::area_size()).unwrap();
            manapi::string::random(bt.data(), bt.size());

            z.push_back(std::move(bt));
        }

        manapi::slice ri;
        manapi::slice ro;

        {
            manapi::compress::gzip_compress dc (9);
            ri = dc.compress(z, true).unwrap();
        }

        {
            manapi::compress::gzip_decompress dd;
            ro = dd.decompress(ri, true).unwrap();
        }

        ASSERT_TRUE(z.cmp(ro) == 0);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

MANAPIHTTP_TESTS_MAIN