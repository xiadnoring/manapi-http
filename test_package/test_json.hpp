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

#define TEST_JSON_TEXT_LARGE R"([{"_id":"686402978cc071126e7518cc","index":0,"guid":"dd9d3e05-50c4-40d9-ae7c-59c80ee323c2","isActive":true,"balance":"$2,886.02","picture":"http://placehold.it/32x32","age":35,"eyeColor":"brown","name":"Woods Lambert","gender":"male","company":"PORTALINE","email":"woodslambert@portaline.com","phone":"+1 (922) 409-3982","address":"892 Post Court, Cressey, Florida, 2988","about":"Elit consequat in eu sint veniam exercitation laboris do aliqua et laborum cillum irure exercitation. Nisi reprehenderit consectetur eu ipsum anim consectetur consequat dolore deserunt Lorem. Aliquip esse enim laboris ipsum do incididunt est voluptate nisi anim. Et qui ut excepteur amet quis exercitation cupidatat qui incididunt consequat voluptate eu velit amet. Fugiat voluptate excepteur adipisicing sunt consequat amet excepteur anim adipisicing irure in tempor qui Lorem. Est ipsum laborum ea ex exercitation commodo proident incididunt laboris consequat in velit laborum ex. Minim aliquip sint irure ad reprehenderit proident elit ipsum minim ex non incididunt.\r\n","registered":"2025-01-20T02:30:14 -05:00","latitude":68.99366,"longitude":45.622174,"tags":["dolore","enim","esse","pariatur","aute","velit","proident"],"friends":[{"id":0,"name":"Aimee Whitaker"},{"id":1,"name":"Ethel Kelley"},{"id":2,"name":"Joyner Blankenship"}],"greeting":"Hello, Woods Lambert! You have 8 unread messages.","favoriteFruit":"apple"},{"_id":"68640297e1c3c2bd123d920a","index":1,"guid":"1abf14dd-2017-47f8-a80e-8e756b71b930","isActive":true,"balance":"$2,114.19","picture":"http://placehold.it/32x32","age":28,"eyeColor":"blue","name":"Lynn Henry","gender":"male","company":"RADIANTIX","email":"lynnhenry@radiantix.com","phone":"+1 (824) 554-2234","address":"228 Vanderbilt Avenue, Derwood, Indiana, 7602","about":"Exercitation cillum cupidatat aute mollit. Qui elit occaecat dolor fugiat est exercitation deserunt cupidatat irure exercitation eiusmod officia. Anim ipsum in fugiat est culpa est laboris. Tempor ea incididunt incididunt reprehenderit adipisicing aute enim commodo. Enim laborum amet do amet duis reprehenderit ex duis.\r\n","registered":"2023-05-21T12:12:21 -05:00","latitude":-2.736757,"longitude":-141.924233,"tags":["ipsum","voluptate","ullamco","reprehenderit","fugiat","adipisicing","mollit"],"friends":[{"id":0,"name":"Marianne Crane"},{"id":1,"name":"Wiley Garrett"},{"id":2,"name":"Blake Cantrell"}],"greeting":"Hello, Lynn Henry! You have 8 unread messages.","favoriteFruit":"banana"},{"_id":"6864029798ca8f185fdea614","index":2,"guid":"ef4f356e-06a2-4eeb-b8bc-10e058755bd7","isActive":false,"balance":"$1,096.47","picture":"http://placehold.it/32x32","age":25,"eyeColor":"green","name":"Lorene Sullivan","gender":"female","company":"OULU","email":"lorenesullivan@oulu.com","phone":"+1 (914) 586-2439","address":"478 Claver Place, National, Louisiana, 2605","about":"Officia id minim tempor laborum aliquip. Fugiat elit ex cillum amet mollit. Elit duis quis culpa velit duis elit duis ipsum laborum labore dolore enim dolore. Est officia sunt deserunt velit eu qui. Amet sint officia est quis laborum cillum consectetur consequat occaecat ex anim fugiat. Et cillum veniam voluptate anim do Lorem mollit laboris.\r\n","registered":"2023-06-13T06:54:38 -05:00","latitude":-46.916333,"longitude":158.237463,"tags":["laboris","sunt","elit","consectetur","amet","dolore","elit"],"friends":[{"id":0,"name":"Janelle Conner"},{"id":1,"name":"Rochelle Harrison"},{"id":2,"name":"Meyer Cummings"}],"greeting":"Hello, Lorene Sullivan! You have 5 unread messages.","favoriteFruit":"apple"},{"_id":"68640297438e24cd4229bab2","index":3,"guid":"63e1e5df-acf7-4b72-be3f-a4b28572f4d8","isActive":false,"balance":"$1,476.24","picture":"http://placehold.it/32x32","age":24,"eyeColor":"blue","name":"Elaine Parks","gender":"female","company":"GYNK","email":"elaineparks@gynk.com","phone":"+1 (859) 440-2956","address":"449 Sullivan Place, Davenport, Washington, 6562","about":"Aute qui est sit proident elit velit sunt reprehenderit officia anim incididunt deserunt est laborum. Excepteur adipisicing reprehenderit elit quis fugiat enim pariatur est veniam irure ut sint. Dolore adipisicing id voluptate ipsum ad duis. Esse aute adipisicing incididunt ex amet ut ad fugiat commodo velit ullamco adipisicing aliqua ex. Officia sunt ea anim ut id tempor tempor pariatur ad occaecat nisi.\r\n","registered":"2020-10-19T10:02:53 -05:00","latitude":86.740813,"longitude":111.854066,"tags":["eu","commodo","duis","ex","dolore","incididunt","occaecat"],"friends":[{"id":0,"name":"Rosales Ramos"},{"id":1,"name":"Adams Warner"},{"id":2,"name":"Haney Sparks"}],"greeting":"Hello, Elaine Parks! You have 5 unread messages.","favoriteFruit":"banana"},{"_id":"68640297f925c92c78796c7c","index":4,"guid":"6067f160-896d-458e-82c7-98a1f9847b1a","isActive":false,"balance":"$1,527.77","picture":"http://placehold.it/32x32","age":20,"eyeColor":"blue","name":"Angie Mathews","gender":"female","company":"PROFLEX","email":"angiemathews@proflex.com","phone":"+1 (986) 408-2771","address":"293 Jaffray Street, Garberville, Delaware, 1606","about":"Occaecat magna irure ex sit ipsum dolore non. Officia cupidatat quis ea adipisicing ad voluptate qui. Quis ut laborum voluptate commodo ipsum quis ex veniam adipisicing labore est. Labore nostrud proident do minim non cillum cupidatat nulla. Quis sit ut pariatur est eu nulla ipsum voluptate quis sit cillum voluptate sint. Magna excepteur in aliqua commodo sunt amet labore aliquip adipisicing magna. Cillum pariatur magna ullamco quis anim minim est occaecat.\r\n","registered":"2022-11-05T04:15:56 -05:00","latitude":0.687171,"longitude":23.239552,"tags":["qui","pariatur","fugiat","amet","ut","laborum","labore"],"friends":[{"id":0,"name":"Leona Rosales"},{"id":1,"name":"Sally Frazier"},{"id":2,"name":"Carolyn Bradley"}],"greeting":"Hello, Angie Mathews! You have 1 unread messages.","favoriteFruit":"strawberry"},{"_id":"68640297d1039d25b495b8bb","index":5,"guid":"12761ae5-a8a1-4b67-9c58-c77e8e64ef20","isActive":true,"balance":"$3,682.30","picture":"http://placehold.it/32x32","age":26,"eyeColor":"blue","name":"Tracey Chang","gender":"female","company":"GEEKOL","email":"traceychang@geekol.com","phone":"+1 (916) 599-2071","address":"329 Woodrow Court, Woodruff, New Mexico, 6363","about":"Cupidatat irure culpa officia cillum incididunt eiusmod laboris. Mollit minim eu do mollit consectetur nisi duis culpa. Elit minim ex do in ullamco ipsum in non. Adipisicing dolore culpa eu nisi pariatur.\r\n","registered":"2017-06-12T01:41:20 -05:00","latitude":68.16672,"longitude":133.909283,"tags":["amet","occaecat","dolor","eu","officia","adipisicing","eu"],"friends":[{"id":0,"name":"Willis Carpenter"},{"id":1,"name":"Wilder Petty"},{"id":2,"name":"Valencia Rich"}],"greeting":"Hello, Tracey Chang! You have 2 unread messages.","favoriteFruit":"strawberry"},{"_id":"686402975219d851ad06f88a","index":6,"guid":"9b689c5f-3912-45bf-8b31-55620cf27c23","isActive":true,"balance":"$3,925.56","picture":"http://placehold.it/32x32","age":22,"eyeColor":"green","name":"Jill Collier","gender":"female","company":"OPTICOM","email":"jillcollier@opticom.com","phone":"+1 (874) 408-3664","address":"461 Joralemon Street, Williamson, Federated States Of Micronesia, 870","about":"Cillum occaecat consectetur elit commodo. Et aliquip qui nisi magna voluptate tempor consequat Lorem culpa est magna proident qui. Dolor ad culpa do ad reprehenderit aute nulla magna sit ullamco nisi in. Nulla id dolore consectetur nisi cillum amet minim magna in est nostrud dolore ipsum.\r\n","registered":"2019-11-20T02:59:53 -05:00","latitude":54.104721,"longitude":142.016516,"tags":["duis","excepteur","qui","anim","ut","ad","qui"],"friends":[{"id":0,"name":"Ballard Burris"},{"id":1,"name":"Mavis Robles"},{"id":2,"name":"Edna Gibbs"}],"greeting":"Hello, Jill Collier! You have 2 unread messages.","favoriteFruit":"banana"}])"

#include <stacktrace>

#include "./utest.h"


UTEST(json, block_parse_1) {
    auto res = manapi::json::parse(R"( { "hello"            : "world" ,   "world":   "hello"   } )");
    ASSERT_EQ(res.ok(), true);
    auto val = res.unwrap();
    ASSERT_TRUE(val["hello"] == "world");
    ASSERT_TRUE(val["world"] == "hello");
}

UTEST(json, block_parse_2) {
    auto rhs = manapi::json::parse(TEST_JSON_TEXT_LARGE);
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res[0]["_id"] == std::string{"686402978cc071126e7518cc"});
}

UTEST(json, block_parse_special_symbols) {
    auto rhs = manapi::json::parse(R"({"hello": "worl\nd"})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["hello"] == "worl\nd");
}

UTEST(json, block_dump_special_symbols) {
    manapi::json a = {{"hello", "world\ntest"}};
    std::string b = a.dump();
    ASSERT_TRUE(b == R"({"hello": "world\ntest"})");
}

UTEST(json, block_parse_unsigned_integer) {
    auto rhs = manapi::json::parse(R"({"int": 18446744073709551615})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_EQ(static_cast<std::size_t>(res["int"].as_integer()), 18446744073709551615UL);
}

UTEST(json, block_parse_bool) {
    auto rhs = manapi::json::parse(R"({"true": true, "false": false})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_EQ(res["true"].as_bool(), true);
    ASSERT_EQ(res["false"].as_bool(), false);
}

UTEST(json, block_parse_integer) {
    auto rhs = manapi::json::parse(R"({"int": 9223372036854775807, "nint": -9223372036854775807})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_EQ(res["int"].as_integer(), 9223372036854775807);
    ASSERT_EQ(res["nint"].as_integer(), -9223372036854775807);
}

UTEST(json, block_parse_decimal) {
    auto rhs = manapi::json::parse(R"({"edec": 1e5, "dec": 10.0, "pi": 3.14159265358979323846})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_EQ(res["edec"].as_decimal(), 1e5);
    ASSERT_EQ(res["dec"].as_decimal(), 10.0);
    auto a = (double)res["pi"].as_decimal();
    auto b = (double)static_cast<double long> (3.14159265358979323846);
    ASSERT_EQ(a, b);
}

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

UTEST(json, block_parse_null) {
    auto rhs = manapi::json::parse(R"({"null": null})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["null"] == nullptr);
}
#ifdef MANAPIHTTP_BIGINT_SUPPORT
UTEST(json, block_parse_bigint_integer) {
    manapi::json_builder res (manapi::json_mask{nullptr}, true, 512);
    res.parse(R"({"bigint": 11111111111111111111111111111111111111111111111})").unwrap();
    auto val = res.get().unwrap();
    auto str = val["bigint"].as_bigint().stringify();
    ASSERT_TRUE(str == "11111111111111111111111111111111111111111111111");
}

UTEST(json, block_parse_bigint_signed_integer) {
    manapi::json_builder res (manapi::json_mask{nullptr}, true, 512);
    res.parse(R"({"bigint": -11111111111111111111111111111111111111111111111})").unwrap();
    auto val = res.get().unwrap();
    auto str = val["bigint"].as_bigint().stringify();
    ASSERT_TRUE(str == "-11111111111111111111111111111111111111111111111");
}

UTEST(json, block_parse_bigint_decimal_integer) {
    manapi::json_builder res (manapi::json_mask{nullptr}, true, 512);
    res.parse(R"({"bigint": 11111111111111111111111111111111111111111111111.11111111111111111111111111111111111111111111111})").unwrap();
    auto val = res.get().unwrap();
    auto str = val["bigint"].as_bigint().stringify();
    ASSERT_TRUE(str == "11111111111111111111111111111111111111111111111.11111111111111111111111111111111111111111111111");
}

UTEST(json, block_parse_bigint_signed_decimal_integer) {
    manapi::json_builder res (manapi::json_mask{nullptr}, true, 512);
    res.parse(R"({"bigint": -11111111111111111111111111111111111111111111111.11111111111111111111111111111111111111111111111})").unwrap();
    auto val = res.get().unwrap();
    auto str = val["bigint"].as_bigint().stringify();
    ASSERT_TRUE(str == "-11111111111111111111111111111111111111111111111.11111111111111111111111111111111111111111111111");
}

UTEST(json, block_parse_bigint_exp_decimal_integer) {
    manapi::json_builder res (manapi::json_mask{nullptr}, true, 512);
    res.parse(R"({"bigint": 1e10})").unwrap();
    auto val = res.get().unwrap();
    auto str = val["bigint"].as_bigint().stringify();
    ASSERT_TRUE(str == "10000000000");
}

UTEST(json, block_parse_array) {
    auto rhs = manapi::json::parse(R"([[[[[[[[[[[[[[[[[[[[[[78]]]]]]]]]]]]]]]]]]]]]])");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    int deep = 0;
    while (res.is_array()) {
        deep++;
        res = std::move(res[0]);
    }
    ASSERT_EQ(deep, 22);
    ASSERT_EQ(res.as_integer(), 78);
}

UTEST(json, block_parse_object) {
    auto rhs = manapi::json::parse(R"({"1":{"2":{"3":{"4":{"5":{"6":{"7":{"8":78}}}}}}}})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    int deep = 0;
    while (res.is_object()) {
        deep++;
        res = std::move(res[std::to_string(deep)]);
    }
    ASSERT_EQ(deep, 8);
    ASSERT_EQ(res.as_integer(), 78);
}

UTEST(json, stream_parse_1) {
    manapi::json_builder builder;
    std::string_view s = R"( { "hello"            : "world" ,   "world":   "hello"   } )";
    for (auto &c : s)
        builder << c;
    auto res = builder.get().unwrap();
    ASSERT_TRUE(res["hello"]=="world");
    ASSERT_TRUE(res["world"]=="hello");
}

UTEST(json, stream_parse_2) {
    manapi::json_builder builder;
    std::string_view s = TEST_JSON_TEXT_LARGE;
    for (auto &c : s)
        builder << c;
    auto res = builder.get().unwrap();
    ASSERT_TRUE(res[0]["_id"] == std::string{"686402978cc071126e7518cc"});
}

UTEST(json, dump) {
    manapi::json a = {{"hello", "world"}};

    ASSERT_TRUE(a.dump() == R"({"hello": "world"})");
}

UTEST(json, dump_2) {
    manapi::json a = {{"hello", "world"}};
    ASSERT_TRUE(manapi::json::parse(a.dump(0, 2)).unwrap()["hello"] == "world");
    a = "hello";
    ASSERT_TRUE(a.dump(0, 2) == R"(  "hello")");
    a = 78;
    ASSERT_TRUE(a.dump(0, 2) == R"(  78)");
}

UTEST(json, dump_3) {
    manapi::json a = {
        {"pi", 3.141},
        {"happy", true},
        {"name", "Niels"},
        {"nothing", nullptr},
        {"answer", {
            {"everything", 42}
        }},
        {"list", {1, 0, 2}},
        {"object", {
            {"currency", "USD"},
            {"value", 42.99}
        }}
    };

    ASSERT_TRUE_MSG( a.dump() == R"({"answer": {"everything": 42}, "happy": true, "list": [1, 0, 2], "name": "Niels", "nothing": null, "object": {"currency": "USD", "value": 42.990000}, "pi": 3.141000})",
        "dump(n)");
    ASSERT_TRUE_MSG(a.dump(2) == R"({
  "answer": {
    "everything": 42
  },
  "happy": true,
  "list": [
    1,
    0,
    2
  ],
  "name": "Niels",
  "nothing": null,
  "object": {
    "currency": "USD",
    "value": 42.990000
  },
  "pi": 3.141000
})", "dump(n, 2)");

    ASSERT_TRUE_MSG(a.dump(2, 2) == R"(  {
    "answer": {
      "everything": 42
    },
    "happy": true,
    "list": [
      1,
      0,
      2
    ],
    "name": "Niels",
    "nothing": null,
    "object": {
      "currency": "USD",
      "value": 42.990000
    },
    "pi": 3.141000
  })", "dump(n, 2, 2)");
}

#endif