//#ifdef MANAPIHTTP_HTTP_AS_EXECUTABLE
#   include "json/ManapiJson.hpp"
#   include "json/ManapiJsonMask.hpp"
#   include "json/ManapiJsonBuilder.hpp"
#include "ManapiInitTools.hpp"
#include "std/ManapiSlice.hpp"
// #else
// #   include <manapihttp/json/ManapiJson.hpp>
// #   include <manapihttp/json/ManapiJsonMask.hpp>
// #   include <manapihttp/json/ManapiJsonBuilder.hpp>
// #endif

#define TEST_JSON_TEXT_LARGE R"([{"_id":"686402978cc071126e7518cc","index":0,"guid":"dd9d3e05-50c4-40d9-ae7c-59c80ee323c2","isActive":true,"balance":"$2,886.02","picture":"http://placehold.it/32x32","age":35,"eyeColor":"brown","name":"Woods Lambert","gender":"male","company":"PORTALINE","email":"woodslambert@portaline.com","phone":"+1 (922) 409-3982","address":"892 Post Court, Cressey, Florida, 2988","about":"Elit consequat in eu sint veniam exercitation laboris do aliqua et laborum cillum irure exercitation. Nisi reprehenderit consectetur eu ipsum anim consectetur consequat dolore deserunt Lorem. Aliquip esse enim laboris ipsum do incididunt est voluptate nisi anim. Et qui ut excepteur amet quis exercitation cupidatat qui incididunt consequat voluptate eu velit amet. Fugiat voluptate excepteur adipisicing sunt consequat amet excepteur anim adipisicing irure in tempor qui Lorem. Est ipsum laborum ea ex exercitation commodo proident incididunt laboris consequat in velit laborum ex. Minim aliquip sint irure ad reprehenderit proident elit ipsum minim ex non incididunt.\r\n","registered":"2025-01-20T02:30:14 -05:00","latitude":68.99366,"longitude":45.622174,"tags":["dolore","enim","esse","pariatur","aute","velit","proident"],"friends":[{"id":0,"name":"Aimee Whitaker"},{"id":1,"name":"Ethel Kelley"},{"id":2,"name":"Joyner Blankenship"}],"greeting":"Hello, Woods Lambert! You have 8 unread messages.","favoriteFruit":"apple"},{"_id":"68640297e1c3c2bd123d920a","index":1,"guid":"1abf14dd-2017-47f8-a80e-8e756b71b930","isActive":true,"balance":"$2,114.19","picture":"http://placehold.it/32x32","age":28,"eyeColor":"blue","name":"Lynn Henry","gender":"male","company":"RADIANTIX","email":"lynnhenry@radiantix.com","phone":"+1 (824) 554-2234","address":"228 Vanderbilt Avenue, Derwood, Indiana, 7602","about":"Exercitation cillum cupidatat aute mollit. Qui elit occaecat dolor fugiat est exercitation deserunt cupidatat irure exercitation eiusmod officia. Anim ipsum in fugiat est culpa est laboris. Tempor ea incididunt incididunt reprehenderit adipisicing aute enim commodo. Enim laborum amet do amet duis reprehenderit ex duis.\r\n","registered":"2023-05-21T12:12:21 -05:00","latitude":-2.736757,"longitude":-141.924233,"tags":["ipsum","voluptate","ullamco","reprehenderit","fugiat","adipisicing","mollit"],"friends":[{"id":0,"name":"Marianne Crane"},{"id":1,"name":"Wiley Garrett"},{"id":2,"name":"Blake Cantrell"}],"greeting":"Hello, Lynn Henry! You have 8 unread messages.","favoriteFruit":"banana"},{"_id":"6864029798ca8f185fdea614","index":2,"guid":"ef4f356e-06a2-4eeb-b8bc-10e058755bd7","isActive":false,"balance":"$1,096.47","picture":"http://placehold.it/32x32","age":25,"eyeColor":"green","name":"Lorene Sullivan","gender":"female","company":"OULU","email":"lorenesullivan@oulu.com","phone":"+1 (914) 586-2439","address":"478 Claver Place, National, Louisiana, 2605","about":"Officia id minim tempor laborum aliquip. Fugiat elit ex cillum amet mollit. Elit duis quis culpa velit duis elit duis ipsum laborum labore dolore enim dolore. Est officia sunt deserunt velit eu qui. Amet sint officia est quis laborum cillum consectetur consequat occaecat ex anim fugiat. Et cillum veniam voluptate anim do Lorem mollit laboris.\r\n","registered":"2023-06-13T06:54:38 -05:00","latitude":-46.916333,"longitude":158.237463,"tags":["laboris","sunt","elit","consectetur","amet","dolore","elit"],"friends":[{"id":0,"name":"Janelle Conner"},{"id":1,"name":"Rochelle Harrison"},{"id":2,"name":"Meyer Cummings"}],"greeting":"Hello, Lorene Sullivan! You have 5 unread messages.","favoriteFruit":"apple"},{"_id":"68640297438e24cd4229bab2","index":3,"guid":"63e1e5df-acf7-4b72-be3f-a4b28572f4d8","isActive":false,"balance":"$1,476.24","picture":"http://placehold.it/32x32","age":24,"eyeColor":"blue","name":"Elaine Parks","gender":"female","company":"GYNK","email":"elaineparks@gynk.com","phone":"+1 (859) 440-2956","address":"449 Sullivan Place, Davenport, Washington, 6562","about":"Aute qui est sit proident elit velit sunt reprehenderit officia anim incididunt deserunt est laborum. Excepteur adipisicing reprehenderit elit quis fugiat enim pariatur est veniam irure ut sint. Dolore adipisicing id voluptate ipsum ad duis. Esse aute adipisicing incididunt ex amet ut ad fugiat commodo velit ullamco adipisicing aliqua ex. Officia sunt ea anim ut id tempor tempor pariatur ad occaecat nisi.\r\n","registered":"2020-10-19T10:02:53 -05:00","latitude":86.740813,"longitude":111.854066,"tags":["eu","commodo","duis","ex","dolore","incididunt","occaecat"],"friends":[{"id":0,"name":"Rosales Ramos"},{"id":1,"name":"Adams Warner"},{"id":2,"name":"Haney Sparks"}],"greeting":"Hello, Elaine Parks! You have 5 unread messages.","favoriteFruit":"banana"},{"_id":"68640297f925c92c78796c7c","index":4,"guid":"6067f160-896d-458e-82c7-98a1f9847b1a","isActive":false,"balance":"$1,527.77","picture":"http://placehold.it/32x32","age":20,"eyeColor":"blue","name":"Angie Mathews","gender":"female","company":"PROFLEX","email":"angiemathews@proflex.com","phone":"+1 (986) 408-2771","address":"293 Jaffray Street, Garberville, Delaware, 1606","about":"Occaecat magna irure ex sit ipsum dolore non. Officia cupidatat quis ea adipisicing ad voluptate qui. Quis ut laborum voluptate commodo ipsum quis ex veniam adipisicing labore est. Labore nostrud proident do minim non cillum cupidatat nulla. Quis sit ut pariatur est eu nulla ipsum voluptate quis sit cillum voluptate sint. Magna excepteur in aliqua commodo sunt amet labore aliquip adipisicing magna. Cillum pariatur magna ullamco quis anim minim est occaecat.\r\n","registered":"2022-11-05T04:15:56 -05:00","latitude":0.687171,"longitude":23.239552,"tags":["qui","pariatur","fugiat","amet","ut","laborum","labore"],"friends":[{"id":0,"name":"Leona Rosales"},{"id":1,"name":"Sally Frazier"},{"id":2,"name":"Carolyn Bradley"}],"greeting":"Hello, Angie Mathews! You have 1 unread messages.","favoriteFruit":"strawberry"},{"_id":"68640297d1039d25b495b8bb","index":5,"guid":"12761ae5-a8a1-4b67-9c58-c77e8e64ef20","isActive":true,"balance":"$3,682.30","picture":"http://placehold.it/32x32","age":26,"eyeColor":"blue","name":"Tracey Chang","gender":"female","company":"GEEKOL","email":"traceychang@geekol.com","phone":"+1 (916) 599-2071","address":"329 Woodrow Court, Woodruff, New Mexico, 6363","about":"Cupidatat irure culpa officia cillum incididunt eiusmod laboris. Mollit minim eu do mollit consectetur nisi duis culpa. Elit minim ex do in ullamco ipsum in non. Adipisicing dolore culpa eu nisi pariatur.\r\n","registered":"2017-06-12T01:41:20 -05:00","latitude":68.16672,"longitude":133.909283,"tags":["amet","occaecat","dolor","eu","officia","adipisicing","eu"],"friends":[{"id":0,"name":"Willis Carpenter"},{"id":1,"name":"Wilder Petty"},{"id":2,"name":"Valencia Rich"}],"greeting":"Hello, Tracey Chang! You have 2 unread messages.","favoriteFruit":"strawberry"},{"_id":"686402975219d851ad06f88a","index":6,"guid":"9b689c5f-3912-45bf-8b31-55620cf27c23","isActive":true,"balance":"$3,925.56","picture":"http://placehold.it/32x32","age":22,"eyeColor":"green","name":"Jill Collier","gender":"female","company":"OPTICOM","email":"jillcollier@opticom.com","phone":"+1 (874) 408-3664","address":"461 Joralemon Street, Williamson, Federated States Of Micronesia, 870","about":"Cillum occaecat consectetur elit commodo. Et aliquip qui nisi magna voluptate tempor consequat Lorem culpa est magna proident qui. Dolor ad culpa do ad reprehenderit aute nulla magna sit ullamco nisi in. Nulla id dolore consectetur nisi cillum amet minim magna in est nostrud dolore ipsum.\r\n","registered":"2019-11-20T02:59:53 -05:00","latitude":54.104721,"longitude":142.016516,"tags":["duis","excepteur","qui","anim","ut","ad","qui"],"friends":[{"id":0,"name":"Ballard Burris"},{"id":1,"name":"Mavis Robles"},{"id":2,"name":"Edna Gibbs"}],"greeting":"Hello, Jill Collier! You have 2 unread messages.","favoriteFruit":"banana"}])"

#include "./utest.h"
#include "./tools.hpp"


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

UTEST(json, block_parse_3) {
    auto rhs = manapi::json::parse(R"(123  hello world)");
    ASSERT_TRUE(!rhs.ok());
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
    ASSERT_TRUE(b == R"({"hello":"world\ntest"})");
}

UTEST(json, block_dump_special_symbols2) {
    manapi::json a = {{"hello", "🇦🇪🏕️👬😎😎😎😎🥴🥴😼😼😼"}};
    std::string b = a.dump();
    ASSERT_TRUE(b == "{\"hello\":\"🇦🇪🏕️👬😎😎😎😎🥴🥴😼😼😼\"}");
}

UTEST(json, block_parse_unsigned_integer) {
    auto rhs = manapi::json::parse(R"({"int": 18446744073709551615})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(std::abs(res["int"].cast_decimal().as_decimal()-18446744073709551615.0) <= 100);
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

UTEST(json, utf8_simple_cyrillic) {
    auto rhs = manapi::json::parse(R"({"hello": "Привет мир", "goodbye": "До свидания"})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["hello"] == "Привет мир");
    ASSERT_TRUE(res["goodbye"] == "До свидания");
}

UTEST(json, utf8_chinese_japanese_korean) {
    auto rhs = manapi::json::parse(R"({
        "chinese": "你好世界",
        "japanese": "こんにちは世界",
        "korean": "안녕하세요 세계"
    })");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["chinese"] == "你好世界");
    ASSERT_TRUE(res["japanese"] == "こんにちは世界");
    ASSERT_TRUE(res["korean"] == "안녕하세요 세계");
}

UTEST(json, utf8_emoji_mixed) {
    auto rhs = manapi::json::parse(R"({
        "flags": "🇺🇸🇬🇧🇫🇷🇩🇪🇯🇵🇨🇳",
        "faces": "😀😁😂🤣😊😍🥰😘",
        "animals": "🐶🐱🐭🐹🐰🦊🐻🐼",
        "food": "🍎🍐🍊🍋🍌🍉🍇🍓"
    })");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["flags"] == "🇺🇸🇬🇧🇫🇷🇩🇪🇯🇵🇨🇳");
    ASSERT_TRUE(res["faces"] == "😀😁😂🤣😊😍🥰😘");
    ASSERT_TRUE(res["animals"] == "🐶🐱🐭🐹🐰🦊🐻🐼");
    ASSERT_TRUE(res["food"] == "🍎🍐🍊🍋🍌🍉🍇🍓");
}

UTEST(json, utf8_special_characters) {
    auto rhs = manapi::json::parse(R"({
        "math": "∑∏∫∂√∞≈≠≤≥",
        "arrows": "←↑→↓↔↕↖↗↘↙",
        "currency": "€£¥₽₩₪₹₫₦",
        "symbols": "©®™§¶•†‡"
    })");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["math"] == "∑∏∫∂√∞≈≠≤≥");
    ASSERT_TRUE(res["arrows"] == "←↑→↓↔↕↖↗↘↙");
    ASSERT_TRUE(res["currency"] == "€£¥₽₩₪₹₫₦");
    ASSERT_TRUE(res["symbols"] == "©®™§¶•†‡");
}

UTEST(json, utf8_long_complex_string) {
    std::string long_utf8 =
        "🌟 Звезда звезда 🌟\\n"
        "🌈 Радуга радуга 🌈\\n"
        "🎵 Музыка музыка 🎵\\n"
        "🚀 Космос космос 🚀\\n"
        "💻 Программирование программирование 💻\\n"
        "🧠 Искусственный интеллект 🧠\\n"
        "🌍 Планета Земля 🌍\\n"
        "☕ Кофе ☕\\n"
        "📚 Книги 📚\\n"
        "🎮 Игры 🎮\\n"
        "🏆 Победа 🏆\\n"
        "⭐ Успех ⭐";
    std::string long_utf82 =
        "🌟 Звезда звезда 🌟\n"
        "🌈 Радуга радуга 🌈\n"
        "🎵 Музыка музыка 🎵\n"
        "🚀 Космос космос 🚀\n"
        "💻 Программирование программирование 💻\n"
        "🧠 Искусственный интеллект 🧠\n"
        "🌍 Планета Земля 🌍\n"
        "☕ Кофе ☕\n"
        "📚 Книги 📚\n"
        "🎮 Игры 🎮\n"
        "🏆 Победа 🏆\n"
        "⭐ Успех ⭐";

    auto rhs = manapi::json::parse("{\"text\": \"" + long_utf8 + "\"}");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["text"] == long_utf82);
}

UTEST(json, utf8_surrogate_pairs) {
    auto rhs = manapi::json::parse(R"({
        "musical": "𝄞𝄢𝄫𝄬",
        "cards": "🂠🂡🂢🂣🂤",
        "cjk_ext": "𠀀𠀁𠀂𠀃",
        "old_italic": "𐌀𐌁𐌂𐌃"
    })");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["musical"].as_string() == "𝄞𝄢𝄫𝄬");
    ASSERT_TRUE(res["cards"].as_string() == "🂠🂡🂢🂣🂤");
    ASSERT_TRUE(res["cjk_ext"].as_string() == "𠀀𠀁𠀂𠀃");
    ASSERT_TRUE(res["old_italic"].as_string() == "𐌀𐌁𐌂𐌃");
}

UTEST(json, utf8_bad_parse1) {
    auto rhs = manapi::json::parse(R"("hello\u0")");
    ASSERT_TRUE(!rhs.ok());
}

UTEST(json, utf8_unicode_escape_mixed) {
    auto rhs = manapi::json::parse(R"({
        "mixed": "\u041F\u0440\u0438\u0432\u0435\u0442 World \uD83C\uDF0D",
        "only_escapes": "\u041F\u0440\u0438\u0432\u0435\u0442 \u043C\u0438\u0440",
        "mixed_with_quotes": "\u041F\u0440\u0438\u0432\u0435\u0442 \"World\" \uD83D\uDE00"
    })");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["mixed"] == "Привет World 🌍");
    ASSERT_TRUE(res["only_escapes"] == "Привет мир");
    ASSERT_TRUE(res["mixed_with_quotes"] == "Привет \"World\" 😀");
}

UTEST(json, debug_unicode_surrogate) {
    auto rhs = manapi::json::parse(R"({"single": "\u041F"})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["single"].as_string()== "П");

    rhs = manapi::json::parse(R"({"emoji": "\uD83C\uDF0D"})");
    ASSERT_TRUE(rhs.ok());
    res = rhs.unwrap();

    std::string emoji = res["emoji"].as_string();

    std::string expected = "\xF0\x9F\x8C\x8D";
    ASSERT_TRUE(emoji== expected);
    ASSERT_TRUE(emoji.size()== 4);
}

UTEST(json, utf8_bidi_text) {
    auto rhs = manapi::json::parse(R"({
        "arabic": "السلام عليكم",
        "hebrew": "שָׁלוֹם",
        "mixed": "Hello السلام عليكم שלום World",
        "rtl_with_numbers": "السلام عليكم 123 Шалом"
    })");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["arabic"] == "السلام عليكم");
    ASSERT_TRUE(res["hebrew"] == "שָׁלוֹם");
    ASSERT_TRUE(res["mixed"] == "Hello السلام عليكم שלום World");
    ASSERT_TRUE(res["rtl_with_numbers"] == "السلام عليكم 123 Шалом");
}

UTEST(json, utf8_multiline_poem) {
    auto rhs = manapi::json::parse(R"({
        "poem": "В лесу родилась ёлочка,\nВ лесу она росла.\nЗимой и летом стройная,\nЗелёная была.\n\n🎄🎄🎄\n\nМетель ей пела песенку:\n«Спи, ёлочка, бай-бай!»\nМороз снежком укутывал:\n«Смотри, не замерзай!»"
    })");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    std::string poem = res["poem"].as_string();
    ASSERT_TRUE(poem.find("В лесу родилась ёлочка") != std::string::npos);
    ASSERT_TRUE(poem.find("🎄") != std::string::npos);
    ASSERT_TRUE(poem.find("\n") != std::string::npos);
}

UTEST(json, utf8_control_chars_escaped) {
    auto rhs = manapi::json::parse(R"({
        "with_newlines": "Привет\nМир\n\tТабуляция",
        "with_quotes": "Она сказала: \"Привет!\"",
        "with_backslashes": "Путь: C:\\Users\\Имя\\Documents",
        "mixed_escapes": "UTF-8: 🚀\nTab:\tDone\nQuote:\"\"\nBackslash:\\"
    })");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["with_newlines"] == "Привет\nМир\n\tТабуляция");
    ASSERT_TRUE(res["with_quotes"] == "Она сказала: \"Привет!\"");
    ASSERT_TRUE(res["with_backslashes"] == "Путь: C:\\Users\\Имя\\Documents");
    ASSERT_TRUE(res["mixed_escapes"] == "UTF-8: 🚀\nTab:\tDone\nQuote:\"\"\nBackslash:\\");
}

UTEST(json, block_parse_null) {
    auto rhs = manapi::json::parse(R"({"null": null})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["null"] == nullptr);
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

    ASSERT_TRUE(a.dump() == R"({"hello":"world"})");
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

    ASSERT_TRUE_MSG( a.dump() == R"({"list":[1,0,2],"answer":{"everything":42},"object":{"value":42.99,"currency":"USD"},"nothing":null,"name":"Niels","happy":true,"pi":3.141})",
        "dump(n)");
    ASSERT_TRUE_MSG(a.dump(2) == R"({
  "list":[
    1,
    0,
    2
  ],
  "answer":{
    "everything":42
  },
  "object":{
    "value":42.99,
    "currency":"USD"
  },
  "nothing":null,
  "name":"Niels",
  "happy":true,
  "pi":3.141
})", "dump(n, 2)");

    ASSERT_TRUE_MSG(a.dump(2, 2) == R"(  {
    "list":[
      1,
      0,
      2
    ],
    "answer":{
      "everything":42
    },
    "object":{
      "value":42.99,
      "currency":"USD"
    },
    "nothing":null,
    "name":"Niels",
    "happy":true,
    "pi":3.141
  })", "dump(n, 2, 2)");
}



UTEST(json, dump_4) {
    auto a = manapi::json::parse("[[[]]]").unwrap();
    ASSERT_TRUE(a.dump(2) == R"([
  [
    []
  ]
])");
    a = manapi::json::parse("[{}]").unwrap();
    ASSERT_TRUE(a.dump(2) == R"([
  {}
])");
    a = manapi::json::parse("[{},{},{},{}]").unwrap();
    ASSERT_TRUE(a.dump(2) == R"([
  {},
  {},
  {},
  {}
])");
    a = manapi::json::parse("[[[]]]").unwrap();
    ASSERT_TRUE(a.dump(2, 4) == R"(    [
      [
        []
      ]
    ])");
    ASSERT_TRUE(a.dump(0, 4) == R"(    [
    [
    []
    ]
    ])");
}

UTEST(json, dump_5) {
    auto ctx = init_ctx(utest_result);
    {
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

        manapi::slice sv;
        a.slice(&sv);
        std::string b = R"({"list":[1,0,2],"answer":{"everything":42},"object":{"value":42.99,"currency":"USD"},"nothing":null,"name":"Niels","happy":true,"pi":3.141})";
        ASSERT_TRUE(!sv.cmp(b.data(), b.size()));
        a.slice(&sv, 2);
        b = R"({
  "list":[
    1,
    0,
    2
  ],
  "answer":{
    "everything":42
  },
  "object":{
    "value":42.99,
    "currency":"USD"
  },
  "nothing":null,
  "name":"Niels",
  "happy":true,
  "pi":3.141
})";
        ASSERT_TRUE(!sv.cmp(b.data(), b.size()));

        a.slice(&sv, 2, 2);
        b = R"(  {
    "list":[
      1,
      0,
      2
    ],
    "answer":{
      "everything":42
    },
    "object":{
      "value":42.99,
      "currency":"USD"
    },
    "nothing":null,
    "name":"Niels",
    "happy":true,
    "pi":3.141
  })";

        ASSERT_TRUE(!sv.cmp(b.data(), b.size()));
    }
    manapi::async::run(ctx->stop());
    wait_ctx(ctx);
}

UTEST(json, object_insert) {
    manapi::json a = manapi::json::object();
    a.insert({"hello", "world"});
    ASSERT_TRUE(a["hello"] == "world");
}

UTEST(json, object_logic) {
    manapi::json a = {
        {"hello", "world"},
        {"world", "hello"}
    };
    a.erase("hello");
    ASSERT_TRUE(!a.contains("hello"));
    a.insert("hello", "world");
    ASSERT_TRUE(a.contains("hello"));
    a.find("hello");
}

UTEST(json, integer_logic) {
    manapi::json a = 100;
    a += 5;
    ASSERT_TRUE(a == 105);
    a -= 10;
    ASSERT_TRUE(a == 95);
    a *= 4;
    ASSERT_TRUE(a == 380);
    a /= 2;
    ASSERT_TRUE(a == 190);
    a = (a == 190);
    ASSERT_TRUE(a.is_bool() && a == true);
}

UTEST(json, string_logic) {
    manapi::json data = "hello";
    ASSERT_TRUE(data.size() == 5);
    data += " world";
    ASSERT_TRUE(data.as_string() == "hello world");
}

UTEST(json, decimal_logic) {
    manapi::json data = 100.0;
    ASSERT_TRUE(data - 100.0 <= 0.1);
    data /= 1.5;
    ASSERT_TRUE(data - 66.667 <= 0.1);
    data *= 2;
    ASSERT_TRUE(data - 133.334 <= 0.1);
    data += 0.6666666;
    ASSERT_TRUE(data - 134 <= 0.1);
    data -= 34;
    ASSERT_TRUE(data - 100 <= 0.1);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
UTEST(json, bigint_logic) {
    manapi::json data = manapi::bigint("100.0");
    ASSERT_TRUE(data - 100.0 <= 0.1);
    data /= 1.5;
    ASSERT_TRUE(data - 66.667 <= 0.1);
    data *= 2;
    ASSERT_TRUE(data - 133.334 <= 0.1);
    data += 0.6666666;
    ASSERT_TRUE(data - 134 <= 0.1);
    data -= 34;
    ASSERT_TRUE(data - 100 <= 0.1);
}
#endif

UTEST(json, bool_logic) {
    manapi::json data = false;
    ASSERT_TRUE(data == false);
    ASSERT_TRUE(data == 0);

    data = true;
    ASSERT_TRUE(data == true);
    ASSERT_TRUE(data == 1);
}

UTEST(json, array_logic) {
    manapi::json data = manapi::json::array({"hello", "world", "no"});
    auto size = data.size();
    ASSERT_TRUE(size == 3);
    std::size_t cnt = 0;
    for (auto &item : data.each()) {
        if (cnt==0) {
            ASSERT_TRUE(item == "hello");
        }
        if (cnt == 1) {
            ASSERT_TRUE(item == "world");
        }
        if (cnt == 2) {
            ASSERT_TRUE(item == "no");
        }
        cnt ++;
    }
    ASSERT_TRUE(cnt == size);

    data.push_back("yes");

    ASSERT_TRUE(data.size() == 4);
}

UTEST(json, integer_comp) {
    manapi::json data = 5;
    ASSERT_TRUE(data == 5);
    ASSERT_TRUE(data < 10);
    ASSERT_TRUE(data > 2);
    ASSERT_TRUE(data > -5);
    ASSERT_TRUE(data >= 5);
    ASSERT_TRUE(data <= 5);
    ASSERT_TRUE(data <= 10);
    ASSERT_TRUE(data >= -20);
    ASSERT_TRUE(data != 2);
}

UTEST(json, decimal_comp) {
    manapi::json data = 0.0;
    ASSERT_TRUE(data == 0.0);
    ASSERT_TRUE(data < 10.0);
    ASSERT_TRUE(data > -10.0);
    ASSERT_TRUE(data >= 0.0);
    ASSERT_TRUE(data <= 0.0);
    ASSERT_TRUE(data >= -0.5);
    ASSERT_TRUE(data <= 10.2);
    ASSERT_TRUE(data != 0.5);
}

#ifdef MANAPIHTTP_BIGINT_SUPPORT
UTEST(json, bigint_comp) {
    manapi::json data = manapi::bigint("0.0");
    ASSERT_TRUE(data == 0.0);
    ASSERT_TRUE(data < 10.0);
    ASSERT_TRUE(data > -10.0);
    ASSERT_TRUE(data >= 0.0);
    ASSERT_TRUE(data <= 0.0);
    ASSERT_TRUE(data >= -0.5);
    ASSERT_TRUE(data <= 10.2);
    ASSERT_TRUE(data != 0.5);
}
#endif

UTEST(json, block_parse_decimal_scientific) {
    auto rhs = manapi::json::parse(R"({"e1": 1e5, "e2": 1E5, "e3": 1e-5, "e4": 1E-5, "e5": 1e+5, "e6": 1E+5})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(std::abs(res["e1"].as_decimal() - 100000.0) <= 0.1);
    ASSERT_TRUE(std::abs(res["e2"].as_decimal() - 100000.0) <= 0.1);
    ASSERT_TRUE(std::abs(res["e3"].as_decimal() - 0.00001) <= 0.1);
    ASSERT_TRUE(std::abs(res["e4"].as_decimal() - 0.00001) <= 0.1);
    ASSERT_TRUE(std::abs(res["e5"].as_decimal() - 100000.0) <= 0.1);
    ASSERT_TRUE(std::abs(res["e6"].as_decimal() - 100000.0) <= 0.1);
}

UTEST(json, block_parse_decimal_scientific_with_dot) {
    auto rhs = manapi::json::parse(R"({"d1": 1.5e3, "d2": 1.5E3, "d3": 1.5e-3, "d4": 1.5E-3, "d5": 1.5e+3, "d6": 1.5E+3})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(std::abs(res["d1"].as_decimal() - 1500.0) <= 0.1);
    ASSERT_TRUE(std::abs(res["d2"].as_decimal() - 1500.0) <= 0.1);
    ASSERT_TRUE(std::abs(res["d3"].as_decimal() - 0.0015) <= 0.1);
    ASSERT_TRUE(std::abs(res["d4"].as_decimal() - 0.0015) <= 0.1);
    ASSERT_TRUE(std::abs(res["d5"].as_decimal() - 1500.0) <= 0.1);
    ASSERT_TRUE(std::abs(res["d6"].as_decimal() - 1500.0) <= 0.1);
}

UTEST(json, block_parse_decimal_leading_dot) {
    auto rhs = manapi::json::parse(R"({"d1": .123, "d2": -.456, "d3": .789e2})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(std::abs(res["d1"].as_decimal() - 0.123) <= 0.1);
    ASSERT_TRUE(std::abs(res["d2"].as_decimal() - -0.456) <= 0.1);
    ASSERT_TRUE(std::abs(res["d3"].as_decimal() - 78.9) <= 0.1);
}

UTEST(json, block_parse_decimal_trailing_dot) {
    auto rhs = manapi::json::parse(R"({"d1": 123., "d2": 456.0, "d3": 789.})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["d1"].as_decimal() == 123.0);
    ASSERT_TRUE(res["d2"].as_decimal() == 456.0);
    ASSERT_TRUE(res["d3"].as_decimal() == 789.0);
}

UTEST(json, block_parse_zero_values) {
    auto rhs = manapi::json::parse(R"({"z1": 0, "z2": 0.0, "z3": 0e0, "z4": -0, "z5": -0.0, "z6": 0e+0})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["z1"].as_integer() == 0);
    ASSERT_TRUE(res["z2"].as_decimal() == 0.0);
    ASSERT_TRUE(res["z3"].as_decimal() == 0.0);
    ASSERT_TRUE(res["z4"].as_integer() == 0);
    ASSERT_TRUE(res["z5"].as_decimal() == 0.0);
    ASSERT_TRUE(res["z6"].as_decimal() == 0.0);
}

UTEST(json, block_parse_negative_numbers) {
    auto rhs = manapi::json::parse(R"({"n1": -123, "n2": -123.456, "n3": -1e-3, "n4": -1.5e-2})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["n1"].as_integer() == -123);
    ASSERT_TRUE(std::abs(res["n2"].as_decimal() - -123.456) <= 0.1);
    ASSERT_TRUE(std::abs(res["n3"].as_decimal() - -0.001) <= 0.1);
    ASSERT_TRUE(std::abs(res["n4"].as_decimal() - -0.015) <= 0.1);
}

UTEST(json, block_parse_overflow_integer) {
    auto rhs = manapi::json::parse(R"({"big": 99999999999999999999})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(std::abs(res["big"].cast_decimal().as_decimal() - 99999999999999999999.0) <= 1000);
}

UTEST(json, block_parse_empty_object) {
    auto rhs = manapi::json::parse(R"({})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res.is_object());
    ASSERT_TRUE(res.size() == 0);
}

UTEST(json, block_parse_empty_array) {
    auto rhs = manapi::json::parse(R"([])");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res.is_array());
    ASSERT_TRUE(res.size() == 0);
}

UTEST(json, block_parse_nested_empty) {
    auto rhs = manapi::json::parse(R"({"empty_obj": {}, "empty_arr": []})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["empty_obj"].is_object());
    ASSERT_TRUE(res["empty_obj"].size() == 0);
    ASSERT_TRUE(res["empty_arr"].is_array());
    ASSERT_TRUE(res["empty_arr"].size() == 0);
}

UTEST(json, block_parse_deep_nesting) {
    auto rhs = manapi::json::parse(R"({"a":{"b":{"c":{"d":{"e":{"f":{"g":{"h":{"i":{"j":{"k":{"l":{"m":{"n":{"o":{"p":{"q":{"r":{"s":{"t":{"u":{"v":{"w":{"x":{"y":{"z":"deep"}}}}}}}}}}}}}}}}}}}}}}}}}})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    auto deep = res;
    for (char c = 'a'; c <= 'z'; c++) {
        std::string key(1, c);
        ASSERT_TRUE(deep.is_object());
        ASSERT_TRUE(deep.contains(key));
        deep = deep[key];
    }
    ASSERT_TRUE(deep == "deep");
}

UTEST(json, block_parse_unicode_escape_sequences) {
    auto rhs = manapi::json::parse(R"({
        "u1": "\u0041",
        "u2": "\u00A9",
        "u3": "\u03A0",
        "u4": "\u041F",
        "u5": "\u6C34",
        "u6": "\uD83C\uDF0D"
    })");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["u1"] == "A");
    ASSERT_TRUE(res["u2"] == "©");
    ASSERT_TRUE(res["u3"] == "Π");
    ASSERT_TRUE(res["u4"] == "П");
    ASSERT_TRUE(res["u5"] == "水");
    ASSERT_TRUE(res["u6"] == "🌍");
}

UTEST(json, block_parse_escape_characters) {
    auto rhs = manapi::json::parse(R"({"escapes": "\"\\\/\b\f\n\r\t"})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["escapes"] == "\"\\/\b\f\n\r\t");
}

UTEST(json, block_parse_unicode_mixed_with_escapes) {
    auto rhs = manapi::json::parse(R"({"mixed": "\u041F\u0440\u0438\u0432\u0435\u0442 \n \uD83C\uDF0D \t \"world\""})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["mixed"] == "Привет \n 🌍 \t \"world\"");
}

UTEST(json, block_parse_invalid_json_unclosed_object) {
    auto rhs = manapi::json::parse(R"({"hello": "world")");
    ASSERT_TRUE(!rhs.ok());
}

UTEST(json, block_parse_invalid_json_unclosed_array) {
    auto rhs = manapi::json::parse(R"([1, 2, 3)");
    ASSERT_TRUE(!rhs.ok());
}

UTEST(json, block_parse_invalid_json_trailing_comma) {
    auto rhs = manapi::json::parse(R"({"hello": "world",})");
    ASSERT_TRUE(rhs.ok());
}

UTEST(json, block_parse_invalid_json_trailing_comma_array) {
    auto rhs = manapi::json::parse(R"([1, 2, 3,])");
    ASSERT_TRUE(rhs.ok());
}

UTEST(json, block_parse_invalid_json_missing_colon) {
    auto rhs = manapi::json::parse(R"({"hello" "world"})");
    ASSERT_TRUE(!rhs.ok());
}

UTEST(json, block_parse_invalid_json_missing_comma) {
    auto rhs = manapi::json::parse(R"({"hello": "world" "world": "hello"})");
    ASSERT_TRUE(!rhs.ok());
}

UTEST(json, block_parse_whitespace_only) {
    auto rhs = manapi::json::parse(R"(   )");
    ASSERT_TRUE(!rhs.ok());
}

UTEST(json, block_parse_single_number) {
    auto rhs = manapi::json::parse(R"(123)");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res.as_integer() == 123);
}

UTEST(json, block_parse_single_decimal) {
    auto rhs = manapi::json::parse(R"(123.456)");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(std::abs(res.as_decimal() - 123.456) <= 0.1);
}

UTEST(json, block_parse_single_string) {
    auto rhs = manapi::json::parse(R"("hello")");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res == "hello");
}

UTEST(json, block_parse_single_bool_true) {
    auto rhs = manapi::json::parse(R"(true)");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res.as_bool() == true);
}

UTEST(json, block_parse_single_bool_false) {
    auto rhs = manapi::json::parse(R"(false)");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res.as_bool() == false);
}

UTEST(json, block_parse_single_null) {
    auto rhs = manapi::json::parse(R"(null)");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res.is_null());
}

UTEST(json, block_parse_array_with_whitespace) {
    auto rhs = manapi::json::parse(R"( [ 1 , 2 , 3 , 4 , 5 ] )");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res.is_array());
    ASSERT_TRUE(res.size() == 5);
    ASSERT_TRUE(res[0] == 1);
    ASSERT_TRUE(res[4] == 5);
}

UTEST(json, block_parse_object_with_whitespace) {
    auto rhs = manapi::json::parse(R"( { "a" : 1 , "b" : 2 , "c" : 3 } )");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res.is_object());
    ASSERT_TRUE(res.size() == 3);
    ASSERT_TRUE(res["a"] == 1);
    ASSERT_TRUE(res["c"] == 3);
}

UTEST(json, block_parse_empty_string) {
    auto rhs = manapi::json::parse(R"({"empty": ""})");
    ASSERT_TRUE(rhs.ok());
    auto res = rhs.unwrap();
    ASSERT_TRUE(res["empty"] == "");
}

UTEST(json, json_dump_check) {
    auto ctx = init_ctx(utest_result);

    manapi::async::run ([&] () -> manapi::future<> {
        manapi::async::run(ctx->stop());
        
        manapi::json test = {
            {"name", "Lenar"},
            {"age", 52},
            {"money", 10.99},
            {"items", {"phone", "computer", "bag", "home", "car", "airplane"}},
            {"isStudent", true},
            {"about", nullptr}
        };

        manapi::slice sv;
        std::string str;

        str = test.dump();
        test.slice(&sv);

#define return co_return
        ASSERT_TRUE(sv.cmp(str.data(), str.size()) == 0);
#undef return
    });


    wait_ctx(ctx);
}

UTEST_MAIN();