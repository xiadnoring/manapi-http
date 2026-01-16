#include "cache/ManapiLRU.hpp"

#include "./utest.h"
#include "cache/ManapiTL.hpp"

UTEST(cache, lru1_put) {
    manapi::lru_cache<std::string, std::string> a (10000);
    a.put("hello", "world", 5);
    a.put("hello2", "world", 5);
    ASSERT_TRUE(*a.get("hello").unwrap() == "world");
    ASSERT_TRUE(*a.get("hello2").unwrap() == "world");
    ASSERT_TRUE(a.used() == 10);
}

UTEST(cache, lru_remove) {
    manapi::lru_cache<std::string, std::string> a (10000);
    a.put("hello", "world", 5);
    a.put("hello2", "world", 5);
    a.remove("hello");
    ASSERT_TRUE(!a.get("hello").ok());
    ASSERT_TRUE(*a.get("hello2").unwrap() == "world");
    ASSERT_TRUE(a.used() == 5);
}

UTEST(cache, lru_overflow) {
    manapi::lru_cache<std::string, std::string> a (10);
    a.put("hello", "world", 5);
    a.put("hello2", "world", 5);
    a.put("hello3", "world", 5);
    ASSERT_TRUE(!a.get("hello").ok());
    ASSERT_TRUE(a.get("hello2").ok());
    ASSERT_TRUE(a.get("hello3").ok());
    ASSERT_TRUE(a.used() == 10);
}

UTEST(cache, lru_priorities) {
    manapi::lru_cache<std::string, std::string> a (10);
    a.put("hello", "world", 5);
    a.put("hello2", "world", 5);
    ASSERT_TRUE(a.get("hello").ok());
    a.put("hello3", "world", 5);
    ASSERT_TRUE(a.get("hello").ok());
    ASSERT_TRUE(!a.get("hello2").ok());
    ASSERT_TRUE(a.get("hello3").ok());
    ASSERT_TRUE(a.used() == 10);
}

UTEST(cache, lru_reset) {
    manapi::lru_cache<std::string, std::string> a (10);
    a.put("hello", "world", 5);
    a.put("hello2", "world", 5);
    ASSERT_TRUE(a.get("hello").ok());
    a.put("hello3", "world", 5);
    ASSERT_TRUE(a.get("hello").ok());
    ASSERT_TRUE(!a.get("hello2").ok());
    ASSERT_TRUE(a.get("hello3").ok());
    ASSERT_TRUE(a.used() == 10);
    a.clear();
    ASSERT_TRUE(!a.get("hello").ok());
    ASSERT_TRUE(!a.get("hello3").ok());
    ASSERT_TRUE(a.used() == 0);
}

UTEST(cache, tl_put) {
    manapi::tl_cache<std::string, std::string> a;
    a.put("hello", "world", std::chrono::milliseconds(10000));
    a.put("hello2", "world", std::chrono::milliseconds(10000));
    ASSERT_TRUE(*a.get("hello").unwrap() == "world");
    ASSERT_TRUE(*a.get("hello2").unwrap() == "world");
}

UTEST(cache, tl_remove) {
    manapi::tl_cache<std::string, std::string> a;
    a.put("hello", "world", std::chrono::milliseconds(10000));
    a.put("hello2", "world", std::chrono::milliseconds(10000));
    a.remove("hello");
    ASSERT_TRUE(!a.get("hello").ok());
    ASSERT_TRUE(*a.get("hello2").unwrap() == "world");
}

UTEST(cache, tl_timeout) {
    manapi::tl_cache<std::string, std::string> a;
    a.put("hello", "world", std::chrono::milliseconds(10000));
    a.put("hello2", "world", std::chrono::milliseconds(0));
    ASSERT_TRUE(!a.get("hello2").ok());
    ASSERT_TRUE(*a.get("hello").unwrap() == "world");
}

UTEST_MAIN();