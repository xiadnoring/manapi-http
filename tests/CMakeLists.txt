cmake_minimum_required(VERSION 3.10)

set(MANAPIHTTP_TEST_TOOL_FILES "utest.h" "tools.hpp")

set(MANAPIHTTP_TESTS_SOURCES test_bigint.cpp test_fs.cpp test_http_and_fetch.cpp test_json.cpp test_json_masks.cpp test_parse.cpp test_http_and_fetch_keep_alive.cpp)
foreach(test ${MANAPIHTTP_TESTS_SOURCES})
    set(test_name ${test}_ctest)
    include_directories(${test_name} "../include" "${CMAKE_BINARY_DIR}/include")
    add_executable(${test_name} ${test} ${MANAPIHTTP_TEST_TOOL_FILES})
    target_link_libraries(${test_name} manapihttp)
    add_test(${test_name} ${test_name})
endforeach()