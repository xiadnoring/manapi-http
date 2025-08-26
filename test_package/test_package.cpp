#include "./utest.h"
#include "./test_parse.hpp"
#include "./test_json.hpp"
#include "./test_json_masks.hpp"
#include "./test_bigint.hpp"
#include "./test_http.hpp"
#include "./test_fetch.hpp"

UTEST_STATE();

int main(int argc, const char *const argv[]) {
    manapi::async::context::threadpoolfs(2);
    manapi::async::context::gbs = manapi::async::context::blockedsignals();

    return utest_main(argc, argv);
}