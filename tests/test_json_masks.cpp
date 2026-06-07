//#ifdef MANAPIHTTP_HTTP_AS_EXECUTABLE
#   include "json/ManapiJson.hpp"
#   include "json/ManapiJsonMask.hpp"
#   include "json/ManapiJsonBuilder.hpp"
#include "ManapiInitTools.hpp"
// #else
// #   include <manapihttp/json/ManapiJson.hpp>
// #   include <manapihttp/json/ManapiJsonMask.hpp>
// #   include <manapihttp/json/ManapiJsonBuilder.hpp>
// #endif

#include "./utest.h"

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

UTEST(json_masks, simple_mask_4) {
    manapi::json_mask mask = {
        {"name", "{string(>=3 <=20)}"},
        {"age", "{integer(>=18 <=99)}"},
        {"work", R"({string("engineer")|string("worker")|string("CEO")})"}
    };
    manapi::json man = {
        {"name", "RichMan"},
        {"age", 78},
        {"work", "CEO"}
    };
    ASSERT_TRUE(mask.valid(man).ok());
    man = {
        {"name", "SmartMan"},
        {"age", 18},
        {"work", "worker"}
    };
    ASSERT_TRUE(mask.valid(man).ok());
    man = {
        {"name", "Rober"},
        {"age", 20},
        {"work", "spy"}
    };
    ASSERT_TRUE(!mask.valid(man).ok());
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

UTEST(json_masks, any_condition_mask) {
    manapi::json_mask mask = {
        {"hello", "{any}"}
    };

    auto res = mask.valid(manapi::json {{"hello", "world"}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json {{"hello", 67}});
    ASSERT_TRUE(res.ok());


    res = mask.valid(manapi::json::object());
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
    ASSERT_EXCEPTION(builder << R"(helllooo"})", manapi::exception);

}

UTEST(json_masks, stream_str_condition_mask_max_mean_2) {
    manapi::json_mask mask = {
        {"hello", R"({string(<16)})"}
    };

    manapi::json_builder builder (mask);
    builder << R"({"hello)";
    builder << R"(": "hello)";
    builder << R"(helllooo"})";

    auto obj = builder.get().unwrap();
}

UTEST(json_masks, str_condition_mask_with_or) {
    manapi::json_mask mask = {
        {"hello", manapi::json_mask::Or(manapi::json::array({R"({string("world")})", R"({integer(>5 <=100)})"}))}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"hello", "world"}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"hello", 78}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"hello", 5}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"hello", "hello"}}).ok());
}

UTEST(json_masks, str_condition_mask_with_array) {
    manapi::init_tools::log_trace_init(manapi::debug::LOG_TRACE_LOW);
    manapi::json_mask mask = {
        {"hello", manapi::json_mask::Array(manapi::json{"{string(>=5 <10)}"}, 1, 3)}
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

UTEST(json_masks, any_сondition_mask) {
    manapi::json_mask mask = {
        {"test", manapi::json_mask::Or(manapi::json::array({
            manapi::json::object({
                {"hello", "{bool(true)}"},
            }),
            manapi::json::object({
                {"z", "{any}"}
            })
        }))}
    };

    manapi::json_builder jb (mask);
    jb << R"({"test": {"hello": true}})";
    ASSERT_TRUE(jb.get().ok());
    jb.set(mask);
    ASSERT_EXCEPTION(jb << R"({"test": {"hello": false}})", std::exception);
    jb.set(mask);
    jb << R"({"test": {"z": []}})";
    ASSERT_TRUE(jb.get().ok());
    jb.set(mask);
    jb << R"({"test": {"z": null}})";
    ASSERT_TRUE(jb.get().ok());
    jb.set(mask);
    jb << R"({"test": {"z": [1]}})";
    ASSERT_TRUE(jb.get().ok());
    jb.set(mask);
    jb << R"({"test": {"z": {"morez": "data", "more": {"more": {"more": "info"}}}}})";
    ASSERT_TRUE(jb.get().ok());
    jb.set(mask);
    ASSERT_EXCEPTION(jb << R"({"test": {"b": {"morez": "data", "more": {"more": {"more": "info"}}}}})", std::exception);
}

UTEST(json_masks, stream_bool_condition_mask_with_arr) {
    manapi::json_mask mask = {
        {"hello", "{bool(true)[=5]}"}
    };

    manapi::json_builder jb (mask);
    jb << R"({"hello":     )";
    jb << R"([true, true, true, true, true]})";
    ASSERT_TRUE(jb.is_ready());
    ASSERT_TRUE(jb.get().unwrap()["hello"].size() == 5);
    jb.set(mask);
    jb << R"({"hello": )";
    jb << R"([true, true, true)";
    ASSERT_EXCEPTION(jb << R"(, true, true, true]})", std::exception);
}

UTEST(json_masks, stream_condition_mask_with_or) {
    manapi::json_mask mask = {
        {"hello", manapi::json_mask::Or({"{string(>=5)}", "{integer(5)}", "{bool(false)}"}, true)}
    };

    std::string_view start = R"({"hello": )";
    std::string_view end = R"(})";

    manapi::json_builder jb (mask);
    jb << start <<  R"("hello world")" << end;
    ASSERT_TRUE(jb.get().unwrap()["hello"] == "hello world");
    jb.set(mask);
    jb << start << "5" << end;
    ASSERT_TRUE(jb.get().unwrap()["hello"] == 5);
    jb.set(mask);
    jb << "{}";
    ASSERT_TRUE(jb.get().unwrap().empty());

    jb.set(mask);
    jb << start << "false" << end;
    ASSERT_TRUE(jb.get().unwrap()["hello"] == false);

    jb.set(mask);
    ASSERT_EXCEPTION(jb << start << "true" << end, std::exception);
}

UTEST(json_masks, stream_condition_mask_none1) {
    manapi::json_mask mask = {
        {"hello", "{string|none}"}
    };

    manapi::json_builder jb (mask);
    jb << R"({"hello":"world"})";
    ASSERT_TRUE(jb.get().unwrap()["hello"] == "world");

    jb.set(mask);
    jb << R"({})";
    ASSERT_TRUE(jb.get().unwrap().empty());
}

UTEST(json_masks, stream_condition_mask_none2) {
    manapi::json_mask mask = {
        {"hello", manapi::json_mask::Or(manapi::json::array({
            "{string}", "{integer}"
        }), true)}
    };

    manapi::json_builder jb (mask);
    jb << R"({"hello":"world"})";
    ASSERT_TRUE(jb.get().unwrap()["hello"] == "world");

    jb.set(mask);
    jb << R"({"hello":78})";
    ASSERT_TRUE(jb.get().unwrap()["hello"] == 78);

    jb.set(mask);
    jb << R"({})";
    ASSERT_TRUE(jb.get().unwrap().empty());

    jb.set(mask);
    ASSERT_EXCEPTION(jb << R"({"hello":78.5})", std::exception);
}

UTEST(json_masks, stream_condition_mask_none3) {
    manapi::json_mask mask = {
        {"man", manapi::json_mask::Or(manapi::json::array({
            {
                {"name", "{string(>=5 <=60)}"},
                {"age", "{integer}"}
            }
        }), true)},
        {"time", "{integer(>=0)}"}
    };

    manapi::json_builder jb (mask);
    jb << R"({"man":{"name":"Timur", "age": 78}, "time": 78})";
    ASSERT_TRUE(jb.get().ok());

    jb.set(mask);
    jb << R"({"time": 78})";
    ASSERT_TRUE(jb.get().ok());

    jb.set(mask);
    jb << R"({"man":{"name":"Leo)";
    ASSERT_EXCEPTION(jb << R"(", "age": 78}, "time": 78})", std::exception);

    jb.set(mask);
    jb << R"({"man":{"name":"Leonardo", "age": 78})";
    ASSERT_EXCEPTION(jb << R"(})", std::exception);

    jb.set(mask);
    jb << R"({)";
    ASSERT_EXCEPTION(jb << R"(})", std::exception);
}

UTEST(json_masks, stream_condition_mask_with_or_2) {
    manapi::json_mask mask = {
        {"data", manapi::json_mask::Or({"{string(=2)}", {
            {"res", "{string(<=5)}"}
        },
        manapi::json_mask::Array("{string(<=5)}", 0, 4)})}
    };

    std::string_view start = R"({"data": )";
    std::string_view end = R"(})";

    manapi::json_builder jb (mask);
    jb << start << R"("OK")" << end;
    ASSERT_TRUE(jb.get().ok());

    jb.set(mask);
    jb << start << R"({"res": "2+2=4"})" << end;
    ASSERT_TRUE(jb.get().ok());

    jb.set(mask);
    jb << start << R"(["2+2=4", "1+1=2", "0=0", "7=8"])" << end;
    ASSERT_TRUE(jb.get().ok());

    jb.set(mask);
    jb << start << R"(["2+2=4", "1+1=2", "0=0", "7=8")";
    ASSERT_EXCEPTION(jb << R"(, "1=1"])" << end, std::exception);

    jb.set(mask);
    jb << start << R"(["2+2=4)";
    jb << R"(", "5+2" )";
    jb << R"(])" << end;
    ASSERT_TRUE(jb.get().ok());
}

UTEST(json_masks, stream_smart_condition_1) {
    manapi::json_mask mask = {
        {"id", "{integer}"},
        {"zone", "{string(<=100)}"},
        {"do", manapi::json_mask::Array(
            manapi::json_mask::Or (manapi::json::array({
                {
                    {"type", "{string(\"force-set\")}"},
                    {"graph", manapi::json_mask::Array(
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
    jb.set(mask);
    data = R"({"id": 1254, "zone": "PANDA", "do": [{"type": "restart"}, {"type": "force-set", "graph": [
["naming", [1, 2, 3, 4, 5, 6, 7], 56, 78, -78, 34, "descriptinodfgdgfdfgd fgdfgdfdg f"]
], "versions": [[56, 23], [12, 67], [0,0]]})";
    for (auto c : data)
        jb << c;
    jb.set(mask);
    ASSERT_EXCEPTION(
    data = R"(, {"type": "restArt"}]})";
    for (auto c : data){
    jb << c;
}, std::exception);
}

UTEST_MAIN();