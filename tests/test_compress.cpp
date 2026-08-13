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

#define MANAPI__COMPRESS_TEXT "Hello World!\n\nMy name is Timur.\nI am 19 years old"

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
            ri = dc.compress(z, false).unwrap();
            ri.push_back(dc.compress( manapi::slice () , true).unwrap());
        }

        {
            manapi::compress::deflate_decompress dd;
            ro = dd.decompress(ri, false).unwrap();
            ro.push_back(dd.decompress(manapi::slice (), true).unwrap());
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

UTEST(compress, compress_gzip_small_data) {
    auto ctx = init_ctx(utest_result);
    {
        assert(sizeof (MANAPI__COMPRESS_TEXT) == 50);
        auto st = std::string (MANAPI__COMPRESS_TEXT);
        manapi::slice z;
        for (int i = 0; i < 2; i++) {
            auto bt = manapi::async::memory_fabric()->buffer(4096).unwrap();
            ::memcpy (bt.data(), st.data(), st.size());

            z.push_back(std::move(bt));
        }

        manapi::slice ri;
        manapi::slice ro;

        auto gg = z.subslice(0, 49).unwrap();

        {
            manapi::compress::gzip_compress dc (9);
            ri = dc.compress(gg, false).unwrap();
            ri.push_back(dc.compress( z.subslice(0, 0).unwrap() , true).unwrap());
        }

        {
            manapi::compress::gzip_decompress dd;
            ro = dd.decompress(ri, true).unwrap();
        }

        ASSERT_TRUE(gg.cmp(ro) == 0);
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

MANAPIHTTP_TESTS_MAIN