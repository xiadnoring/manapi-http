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
        {"hello", R"({decimal(=1e5)})"}
    };

    auto res = mask.valid(manapi::json {{"hello", 1e5}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", 4.68}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, dec_condition_mask_value) {
    manapi::json_mask mask = {
        {"hello", R"({decimal(16e5)})"}
    };

    auto res = mask.valid(manapi::json {{"hello", 16e5}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", 78.3}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, stream_str_condition_mask_max_mean) {
    manapi::json_mask mask = {
        {"hello", R"({string(<8)})"}
    };

    manapi::json_builder builder (mask);
    builder << R"({"hello)";
    builder << R"(": "hello)";
    ASSERT_EXCEPTION(builder << R"(helllooo"})", std::exception);

}

UTEST(json_masks, stream_str_condition_mask_max_mean_2) {
    manapi::json_mask mask = {
        {"hello", R"({string(<16)})"}
    };

    manapi::json_builder builder (mask);
    builder << R"({"hello)";
    builder << R"(": "hello)";
    builder << R"(helllooo"})";

    auto obj = builder.get().value();
}

UTEST(json_masks, str_condition_mask_with_or) {
    manapi::json_mask mask = {
        {"hello", manapi::json_mask::OR(manapi::json::array({R"({string("world")})", R"({integer(>5 <=100)})"}))}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"hello", "world"}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"hello", 78}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"hello", 5}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"hello", "hello"}}).ok());
}

UTEST(json_masks, str_condition_mask_with_array) {
    manapi::json_mask mask = {
        {"hello", manapi::json_mask::ARRAY(manapi::json{"{string(>=5 <10)}"}, 1, 3)}
    };

    auto res = mask.valid(manapi::json {{"hello", manapi::json::array({"world", "world", "world"})}});
    ASSERT_TRUE(res.ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"hello", manapi::json::array({"world"})}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"hello", manapi::json::array()}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"hello", manapi::json::array({"hello", "hello", "hello", "hello"})}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"hello", manapi::json::array({"hello", "hell", "hello"})}}).ok());
}

UTEST(json_masks, str_condition_mask_with_arr) {
    manapi::json_mask mask = {
        {"hello", "{string(>=5 <10)[>0 <4]}"}
    };

    auto res = mask.valid(manapi::json {{"hello", manapi::json::array({"world", "world", "world"})}});
    ASSERT_TRUE(res.ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"hello", manapi::json::array({"world"})}}).ok());
    res = mask.valid(manapi::json {{"hello", manapi::json::array()}});
    ASSERT_TRUE(!res.ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"hello", manapi::json::array({"hello", "hello", "hello", "hello"})}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"hello", manapi::json::array({"hello", "hell", "hello"})}}).ok());
}

UTEST(json_masks, bool_condition_mask_with_arr) {
    manapi::json_mask mask = {
        {"hello", "{bool(true)[=5]}"}
    };

    auto res = mask.valid(manapi::json {{"hello", {true, true, true, true, true}}});
    ASSERT_TRUE(res.ok());
    res = mask.valid(manapi::json {{"hello", {true, true, false, true, true}}});
    ASSERT_TRUE(!res.ok());
    res = mask.valid(manapi::json {{"hello", {true, true, true, true}}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, stream_bool_condition_mask_with_arr) {
    manapi::json_mask mask = {
        {"hello", "{bool(true)[=5]}"}
    };

    manapi::json_builder jb (mask);
    jb << R"({"hello":     )";
    jb << R"([true, true, true, true, true]})";
    ASSERT_TRUE(jb.is_ready());
    ASSERT_TRUE(jb.get().value()["hello"].size() == 5);

    jb << R"({"hello": )";
    jb << R"([true, true, true)";
    ASSERT_EXCEPTION(jb << R"(, true, true, true]})", std::exception);
}

UTEST(json_masks, stream_condition_mask_with_or) {
    manapi::json_mask mask = {
        {"hello", manapi::json_mask::OR({"{string(>=5)}", "{integer(5)}", "{bool(false)}"}, true)}
    };

    std::string_view start = R"({"hello": )";
    std::string_view end = R"(})";

    manapi::json_builder jb (mask);
    jb << start <<  R"("hello world")" << end;
    ASSERT_TRUE(jb.get().value()["hello"] == "hello world");

    jb << start << "5" << end;
    ASSERT_TRUE(jb.get().value()["hello"] == 5);

    jb << "{}";
    ASSERT_TRUE(jb.get().value().empty());


    jb << start << "false" << end;
    ASSERT_TRUE(jb.get().value()["hello"] == false);

    ASSERT_EXCEPTION(jb << start << "true" << end, std::exception);
}

UTEST(json_masks, stream_condition_mask_with_or_2) {
    manapi::json_mask mask = {
        {"data", manapi::json_mask::OR({"{string(=2)}", {
            {"res", "{string(<=5)}"}
        },
        manapi::json_mask::ARRAY("{string(<=5)}", 0, 4)})}
    };

    std::string_view start = R"({"data": )";
    std::string_view end = R"(})";

    manapi::json_builder jb (mask);
    jb << start << R"("OK")" << end;
    ASSERT_TRUE(jb.get().ok());

    jb << start << R"({"res": "2+2=4"})" << end;
    ASSERT_TRUE(jb.get().ok());


    jb << start << R"(["2+2=4", "1+1=2", "0=0", "7=8"])" << end;
    ASSERT_TRUE(jb.get().ok());

    jb << start << R"(["2+2=4", "1+1=2", "0=0", "7=8")";
    ASSERT_EXCEPTION(jb << R"(, "1=1"])" << end, std::exception);

    jb.clear();

    jb << start << R"(["2+2=4)";
    jb << R"(", "5+2" )";
    jb << R"(])" << end;
    ASSERT_TRUE(jb.get().ok());
}

UTEST(json_masks, stream_smart_condition_1) {
    manapi::json_mask mask = {
        {"id", "{integer}"},
        {"zone", "{string(<=100)}"},
        {"do", manapi::json_mask::ARRAY(
            manapi::json_mask::OR (manapi::json::array({
                {
                    {"type", "{string(\"force-set\")}"},
                    {"graph", manapi::json_mask::ARRAY(
                        manapi::json::array({"{string(<=150)}", "{integer(>=0)[<=1000]}", "{integer(>=0)}", "{integer(>=0)}",
                            "{integer()}", "{integer()}", "{string(<=2500)}"})
                    )},
                    {"versions", "{integer[=2][<=1000]}"}
                },
                {
                    {"type", "{string(\"restart\")}"}
                }
            }))
        )}
    };

    manapi::json_builder jb (mask);
    std::string_view data = R"({"id": 1254, "zone": "PANDA", "do": [{"type": "restart"}, {"type": "force-set", "graph": [
["naming", [1, 2, 3, 4, 5, 6, 7], 56, 78, -78, 34, "descriptinodfgdgfdfgd fgdfgdfdg f"]
], "versions": [[56, 23], [12, 67], [0,0]]}, {"type": "restart"}]})";
    for (auto c : data)
        jb << c;
    ASSERT_TRUE(jb.get().ok());
    data = R"({"id": 1254, "zone": "PANDA", "do": [{"type": "restart"}, {"type": "force-set", "graph": [
["naming", [1, 2, 3, 4, 5, 6, 7], 56, 78, -78, 34, "descriptinodfgdgfdfgd fgdfgdfdg f"]
], "versions": [[56, 23], [12, 67], [0,0]]})";
    for (auto c : data)
        jb << c;
    ASSERT_EXCEPTION(
    data = R"(, {"type": "restArt"}]})";
    for (auto c : data){
    jb << c;
}, std::exception);
}