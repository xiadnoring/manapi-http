#include "./utest.h"

#ifdef MANAPIHTTP_HTTP_AS_EXECUTABLE
#   include "ManapiInitTools.hpp"
#   include "ManapiProcess.hpp"
#   include "json/ManapiJson.hpp"
#else
#   include <manapihttp/ManapiInitTools.hpp>
#   include <manapihttp/ManapiProcess.hpp>
#   include <manapihttp/json/ManapiJson.hpp>
#endif

UTEST(json, block_parse_string) {
    auto rhs = manapi::json::parse(R"({"str": "hello"})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["str"] == "hello");
}

UTEST(json, block_parse_string_unicode) {
    auto rhs = manapi::json::parse(R"({"str": "🇦🇪🏕️👬😎😎😎😎🥴🥴😼😼😼"})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["str"] == "🇦🇪🏕️👬😎😎😎😎🥴🥴😼😼😼");

    rhs = manapi::json::parse(R"({"\u003c\u003c\u003c\u0026gt": "\u003c\u003c\u003c\u0026gt"})");
    ASSERT_TRUE(rhs.ok());
    res = rhs.unwrap();
    ASSERT_TRUE(res["<<<&gt"] == "<<<&gt");
}

UTEST_MAIN();