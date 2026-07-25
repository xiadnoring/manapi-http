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

UTEST(json_masks, any_condition_mask1) {
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

UTEST(json_masks, any_condition_mask2) {
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

UTEST(json_masks, stream_smart_condition_2) {
    manapi::json_mask mask = {
        {"id", "{integer}"},
        {"zone", "{string(<=100)}"},
        {"do", manapi::json_mask::Array(
            manapi::json_mask::Or (manapi::json::array({
                manapi::json::array({0, 1, 2, 3, 4, 5, 6}),
                manapi::json::array({78})
            }))
        )}
    };

    manapi::json_builder jb (mask);
    std::string_view data = R"({"id": 1254, "zone": "PANDA", "do": [[78], [0,1,2,3, 4,   5, 6 ], [ 78], [78  ], [ 0,1,2,3,4,5,6]]})";
    for (auto c : data)
        jb << c;
    ASSERT_TRUE(jb.get().ok());
    jb.set(mask);
    data = R"({"id": 1254, "zone": "PANDA", "do": [[0, 1 , 2 ,   3, 4,5,6],[0, 1 , 2 ,   3, 4,5,6)";
    for (auto c : data)
        jb << c;
    jb.set(mask);
    ASSERT_EXCEPTION(
    data = R"(,7], [78]]})";
    for (auto c : data){
    jb << c;
    }, std::exception);

    jb.set(mask);
    data = R"({"id": 1254, "zone": "PANDA", "do": [[0, 1 , 2 ,   3, 4,5,6],[0, 1 , 2 ,   3, 4,5,6],[78],)";
    for (auto c : data)
        jb << c;
    jb.set(mask);
    ASSERT_EXCEPTION(
    data = R"([78, 0, 1, 2, 3, 4, 5 , 6]]})";
    for (auto c : data){
    jb << c;
    }, std::exception);

    jb.set(mask);
    data = R"({"id": 1254, "zone": "PANDA", "do": [[78],)";
    for (auto c : data)
        jb << c;
    jb.set(mask);
    ASSERT_EXCEPTION(
    data = R"([6,5,4,3,2,1,0]]})";
    for (auto c : data){
    jb << c;
}, std::exception);
}

UTEST(json_masks, array_min_max_conditions) {
    manapi::json_mask mask = {
        {"items", manapi::json_mask::Array("{integer(>=0 <=100)}", 2, 5)}
    };

    auto res = mask.valid(manapi::json {{"items", {1, 2, 3}}});
    ASSERT_TRUE(res.ok());

    res = mask.valid(manapi::json {{"items", {1, 2}}});
    ASSERT_TRUE(res.ok());

    res = mask.valid(manapi::json {{"items", {1, 2, 3, 4, 5}}});
    ASSERT_TRUE(res.ok());

    res = mask.valid(manapi::json {{"items", {1}}});
    ASSERT_TRUE(!res.ok());

    res = mask.valid(manapi::json {{"items", {1, 2, 3, 4, 5, 6}}});
    ASSERT_TRUE(!res.ok());

    res = mask.valid(manapi::json {{"items", {1, 200, 3}}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, array_exact_length) {
    manapi::json_mask mask = {
        {"data", manapi::json_mask::Array("{string(>=3)}", 3, 3)}
    };

    auto res = mask.valid(manapi::json {{"data", {"one", "two", "three"}}});
    ASSERT_TRUE(res.ok());

    res = mask.valid(manapi::json {{"data", {"one", "two"}}});
    ASSERT_TRUE(!res.ok());

    res = mask.valid(manapi::json {{"data", {"one", "two", "three", "four"}}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, nested_objects) {
    manapi::json_mask mask = {
        {"user", {
            {"name", "{string(>=3 <=50)}"},
            {"address", {
                {"city", "{string(>=2)}"},
                {"zip", "{integer(>=10000 <=99999)}"}
            }},
            {"contacts", {
                {"email", "{string(>=5)}"},
                {"phone", "{string(>=10)}"}
            }}
        }}
    };

    auto res = mask.valid(manapi::json {
        {"user", {
            {"name", "John Doe"},
            {"address", {
                {"city", "New York"},
                {"zip", 10001}
            }},
            {"contacts", {
                {"email", "john@example.com"},
                {"phone", "+1234567890"}
            }}
        }}
    });
    ASSERT_TRUE(res.ok());

    res = mask.valid(manapi::json {
        {"user", {
            {"name", "John Doe"},
            {"address", {
                {"city", "NY"},
                {"zip", 999}
            }},
            {"contacts", {
                {"email", "john@example.com"},
                {"phone", "+1234567890"}
            }}
        }}
    });
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, optional_fields) {
    manapi::json_mask mask = {
        {"required", "{integer(>=0)}"},
        {"optional", manapi::json_mask::Or(manapi::json::array({"{string}"}), true)}
    };

    auto res = mask.valid(manapi::json {{"required", 5}});
    ASSERT_TRUE(res.ok());

    res = mask.valid(manapi::json {{"required", 5}, {"optional", "hello"}});
    ASSERT_TRUE(res.ok());

    res = mask.valid(manapi::json {{"required", 5}, {"optional", 123}});
    ASSERT_TRUE(!res.ok());
}

UTEST(json_masks, complex_or_conditions) {
    manapi::json_mask mask = {
        {"value", manapi::json_mask::Or({
            "{string(\"yes\")}",
            "{string(\"no\")}",
            "{integer(0)}",
            "{integer(1)}",
            "{bool(true)}",
            "{bool(false)}"
        })}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"value", "yes"}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"value", "no"}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"value", 0}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"value", 1}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"value", true}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"value", false}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"value", "maybe"}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"value", 2}}).ok());
}

UTEST(json_masks, decimal_precision) {
    manapi::json_mask mask = {
        {"price", "{decimal(>=0.01 <1000)}"}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"price", 99.99}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"price", 0.01}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"price", 999.99}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"price", 0.001}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"price", 1000.00}}).ok());
}

UTEST(json_masks, string_patterns) {
    manapi::json_mask mask = {
        {"username", "{string(>=3 <=20)}"},
        {"password", "{string(>=8)}"}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"username", "john_doe"}, {"password", "secret123"}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"username", "jo"}, {"password", "secret123"}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"username", "john_doe"}, {"password", "123"}}).ok());
}

UTEST(json_masks, mixed_array_types) {
    manapi::json_mask mask = {
        {"mixed", manapi::json_mask::Array(manapi::json_mask::Or({
            "{string}",
            "{integer}",
            "{bool}"
        }), 1, 5)}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"mixed", {"hello", 42, true, "world", 100}}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"mixed", {true, false, true}}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"mixed", {}}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"mixed", {1, 2, 3, 4, 5, 6}}}).ok());
}

UTEST(json_masks, deep_nesting_with_arrays) {
    manapi::json_mask mask = {
        {"root", {
            {"level1", {
                {"level2", manapi::json_mask::Array({
                    {"id", "{integer(>=0)}"},
                    {"name", "{string(>=2)}"},
                    {"tags", manapi::json_mask::Array("{string(>=1)}", 0, 3)}
                }, 1, 10)}
            }}
        }}
    };

    auto obj = manapi::json {
        {"root", {
            {"level1", {
                {"level2", {
                    {
                        {"id", 1},
                        {"name", "item1"},
                        {"tags", manapi::json::array({"tag1", "tag2"})}
                    },
                    {
                        {"id", 2},
                        {"name", "item2"},
                        {"tags", manapi::json::array()}
                    }
                }}
            }}
        }}
    };
    ASSERT_TRUE(mask.valid(obj).ok());
}

UTEST(json_masks, equal_condition) {
    manapi::json_mask mask = {
        {"status", "{string(\"active\")}"},
        {"code", "{integer(200)}"},
        {"flag", "{bool(true)}"}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"status", "active"}, {"code", 200}, {"flag", true}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"status", "inactive"}, {"code", 200}, {"flag", true}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"status", "active"}, {"code", 404}, {"flag", true}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"status", "active"}, {"code", 200}, {"flag", false}}).ok());
}

UTEST(json_masks, range_with_exclusive) {
    manapi::json_mask mask = {
        {"age", "{integer(>0 <150)}"},
        {"score", "{integer(>=0 <=100)}"}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"age", 25}, {"score", 85}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"age", 0}, {"score", 85}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"age", 150}, {"score", 85}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"age", 25}, {"score", -1}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"age", 25}, {"score", 101}}).ok());
}

UTEST(json_masks, empty_array_validation) {
    manapi::json_mask mask = {
        {"items", manapi::json_mask::Array("{integer}", 0, 0)}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"items", manapi::json::array({})}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"items", manapi::json::array({1})}}).ok());
}

UTEST(json_masks, null_values) {
    manapi::json_mask mask = {
        {"nullable", manapi::json_mask::Or({
            "{null}",
            "{string}",
            "{integer}"
        })}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"nullable", nullptr}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"nullable", "text"}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"nullable", 42}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"nullable", true}}).ok());
}

UTEST(json_masks, array_of_objects) {
    manapi::json_mask mask = {
        {"users", manapi::json_mask::Array({
            {"name", "{string(>=2)}"},
            {"age", "{integer(>=0 <=120)}"}
        }, 1, 100)}
    };

    auto obj = manapi::json {
        {"users", {
            {{"name", "Alice"}, {"age", 25}},
            {{"name", "Bob"}, {"age", 30}}
        }}
    };
    ASSERT_TRUE(mask.valid(obj).ok());

    obj = manapi::json {{"users", {}}};
    ASSERT_TRUE(!mask.valid(obj).ok());

    obj = manapi::json {
        {"users", {
            {{"name", "A"}, {"age", 25}}
        }}
    };
    ASSERT_TRUE(!mask.valid(obj).ok());
}

UTEST(json_masks, complex_stream_validation) {
    manapi::json_mask mask = {
        {"data", {
            {"type", "{string(\"user\")}"},
            {"attributes", {
                {"id", "{integer(>=1)}"},
                {"name", "{string(>=2 <=100)}"},
                {"email", "{string(>=5)}"}
            }}
        }}
    };

    manapi::json_builder builder(mask);
    builder << R"({"data":{"type":"user","attributes":{"id":123,"name":"John","email":"john@test.com"}}})";
    auto res = builder.get();
    ASSERT_TRUE(res.ok());

    builder.set(mask);
    ASSERT_EXCEPTION(builder << R"({"data":{"type":"user","attributes":{"id":-1,"name":"John","email":"john@test.com"}}})", std::exception);
}

UTEST(json_masks, multiple_or_levels) {
    manapi::json_mask mask = {
        {"result", manapi::json_mask::Or({

                "{string(\"success\")}",
                "{string(\"ok\")}",
                "{integer(200)}",
                "{integer(201)}"
        })}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"result", "success"}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"result", "ok"}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"result", 200}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"result", 201}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"result", "error"}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"result", 404}}).ok());
}

UTEST(json_masks, strict_length_string) {
    manapi::json_mask mask = {
        {"code", "{string(=6)}"}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"code", "ABCDEF"}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"code", "ABC"}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"code", "ABCDEFG"}}).ok());
}

UTEST(json_masks, decimal_scientific_notation) {
    manapi::json_mask mask = {
        {"value", "{decimal(=1e5)}"}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"value", 100000.0}}).ok());
    ASSERT_TRUE(mask.valid(manapi::json {{"value", 1e5}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"value", 99999.9}}).ok());
}

UTEST(json_masks, nested_arrays) {
    manapi::json_mask mask = {
        {"matrix", manapi::json_mask::Array(
            manapi::json_mask::Array("{integer}", 2, 2),
            1, 3
        )}
    };

    auto obj = manapi::json {
        {"matrix", {
            {1, 2},
            {3, 4}
        }}
    };
    ASSERT_TRUE(mask.valid(obj).ok());

    obj = manapi::json {
        {"matrix", {
            {1, 2, 3}
        }}
    };
    ASSERT_TRUE(!mask.valid(obj).ok());

    obj = manapi::json {
        {"matrix", {
            {1, 2},
            {3, 4},
            {5, 6}
        }}
    };
    ASSERT_TRUE(mask.valid(obj).ok());
}

UTEST(json_masks, integer_zero_validation) {
    manapi::json_mask mask = {
        {"count", "{integer(=0)}"}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"count", 0}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"count", 1}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"count", -1}}).ok());
}

UTEST(json_masks, boolean_conditions) {
    manapi::json_mask mask = {
        {"active", "{bool(true)}"},
        {"deleted", "{bool(false)}"}
    };

    ASSERT_TRUE(mask.valid(manapi::json {{"active", true}, {"deleted", false}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"active", false}, {"deleted", false}}).ok());
    ASSERT_TRUE(!mask.valid(manapi::json {{"active", true}, {"deleted", true}}).ok());
}


UTEST_MAIN();