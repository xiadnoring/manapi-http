#pragma once

#ifdef MANAPIHTTP_HTTP_AS_EXECUTABLE
#   include "ManapiJson.hpp"
#   include "ManapiJsonMask.hpp"
#   include "ManapiJsonBuilder.hpp"
#else
#   include <manapihttp/ManapiJson.hpp>
#   include <manapihttp/ManapiJsonMask.hpp>
#   include <manapihttp/ManapiJsonBuilder.hpp>
#endif

UTEST(json_masks, simple_mask_1) {
    manapi::json_mask mask = {
        {"hello", "world"}
    };
    manapi::json a = {{"hello", "world"}};
    ASSERT_TRUE(mask.valid(a).ok());
}

UTEST(json_masks, simple_mask_2) {
    manapi::json_mask mask = {"hello"};
    manapi::json a = {"hello"};
    ASSERT_TRUE(mask.valid(a).ok());
}

UTEST(json_masks, simple_mask_3) {
    manapi::json_mask mask = {
        {"hello", {
            {"arr", {"hello", "world", {
                {"test", 78}
            }}}
        }}
    };
    manapi::json a = {
        {"hello", {
            {"arr", {"hello", "world", {
                {"test", 78}
            }}}
        }}
    };
    auto res = mask.valid(a);
    ASSERT_TRUE(res.ok());

    a = {
        {"hello", {
                {"arr", {"hello", "world", {
                    {"test", 56}
                }}}
        }}
    };

    res = mask.valid(a);
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, str_condition_mask_max_mean) {
    manapi::json_mask mask = {
        {"hello", "{string(<=5)}"}
    };

    auto res = mask.valid(manapi::json {{"hello", "world"}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", "world $2"}});
    ASSERT_TRUE(!res.ok());
}


UTEST(json_masks, str_condition_mask_min_mean) {
    manapi::json_mask mask = {
        {"hello", "{string(>=5)}"}
    };

    auto res = mask.valid(manapi::json {{"hello", "world"}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", "$2"}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, str_condition_mask_mean) {
    manapi::json_mask mask = {
        {"hello", R"({string(=3)})"}
    };

    auto res = mask.valid(manapi::json {{"hello", "123"}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", "1"}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, str_condition_mask_value) {
    manapi::json_mask mask = {
        {"hello", R"({string("hello")})"}
    };

    auto res = mask.valid(manapi::json {{"hello", "hello"}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", "world"}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, int_condition_mask_max_mean) {
    manapi::json_mask mask = {
        {"hello", "{integer(<=5)}"}
    };

    auto res = mask.valid(manapi::json {{"hello", 5}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", 10}});
    ASSERT_TRUE(!res.ok());
}


UTEST(json_masks, int_condition_mask_min_mean) {
    manapi::json_mask mask = {
        {"hello", "{integer(>=5)}"}
    };

    auto res = mask.valid(manapi::json {{"hello", 5}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", 2}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, int_condition_mask_mean) {
    manapi::json_mask mask = {
        {"hello", R"({integer(=3)})"}
    };

    auto res = mask.valid(manapi::json {{"hello", 3}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", 4}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, int_condition_mask_value) {
    manapi::json_mask mask = {
        {"hello", R"({integer(56)})"}
    };

    auto res = mask.valid(manapi::json {{"hello", 56}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", 78}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, dec_condition_mask_max_mean) {
    manapi::json_mask mask = {
        {"hello", "{decimal(<=5)}"}
    };

    auto res = mask.valid(manapi::json {{"hello", 4.6}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", 5.01}});
    ASSERT_TRUE(!res.ok());
}


UTEST(json_masks, dec_condition_mask_min_mean) {
    manapi::json_mask mask = {
        {"hello", "{decimal(>=5.7)}"}
    };

    auto res = mask.valid(manapi::json {{"hello", 5.7}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", 5.69}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, dec_condition_mask_mean) {
    manapi::json_mask mask = {
        {"hello", R"({decimal(=3.78)})"}
    };

    auto res = mask.valid(manapi::json {{"hello", 3.78}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", 4.68}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, dec_condition_mask_value) {
    manapi::json_mask mask = {
        {"hello", R"({decimal(56.6)})"}
    };

    auto res = mask.valid(manapi::json {{"hello", 56.6}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", 78.3}});
    ASSERT_TRUE(!res.ok());
}