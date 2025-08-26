cmake_minimum_required(VERSION 3.12)
project(test_package LANGUAGES CXX)

find_package(manapihttp REQUIRED CONFIG)

add_executable(${PROJECT_NAME} test_package.cpp utest.h
        # Unit Tests
        test_parse.hpp
        test_bigint.hpp
        test_json_masks.hpp
        test_json.hpp
        test_http.hpp
        test_http_and_fetch.cpp
        test_fetch.hpp
        test_fs.cpp
        test_fs.hpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE manapihttp::manapihttp)