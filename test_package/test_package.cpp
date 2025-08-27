#include "./utest.h"
#include "./test_parse.hpp"
#include "./test_json.hpp"
#include "./test_json_masks.hpp"
#include "./test_bigint.hpp"
#include "./test_http.hpp"
#include "./test_fetch.hpp"
#include "./test_fs.hpp"

#ifdef MANAPIHTTP_HTTP_AS_EXECUTABLE
#   include "ManapiInitTools.hpp"
#   include "ManapiProcess.hpp"
#else
#   include <manapihttp/ManapiInitTools.hpp>
#   include <manapihttp/ManapiProcess.hpp>
#endif

UTEST_STATE();

int main(int argc, const char *const argv[]) {

    try {
        manapi::init_tools::log_trace_init((manapi::debug::trace_level)std::stoi(manapi::process::get_env("MANAPIHTTP_LOGTRACE").unwrap()));
    }
    catch (...) {
        manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_NONE);
    }

    manapi::async::context::threadpoolfs(2);
    manapi::async::context::gbs = manapi::async::context::blockedsignals();

    return utest_main(argc, argv);
}