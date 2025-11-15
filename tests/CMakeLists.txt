cmake_minimum_required(VERSION 3.28)

add_compile_options($<$<CXX_COMPILER_ID:GNU>:-fmerge-all-constants>)
# add_compile_options($<$<CXX_COMPILER_ID:GNU>:-fmodules>)
# add_compile_options($<$<CXX_COMPILER_ID:GNU>:-fmodules-ts>)

set(MANAPIHTTP_TEST_TOOL_FILES "utest.h" "tools.hpp")

set(MANAPIHTTP_TESTS_SOURCES test_bigint.cpp test_function.cpp)
foreach(test ${MANAPIHTTP_TESTS_SOURCES})
    set(test_name ${test}_ctest)
    include_directories(${test_name} "../include" "${CMAKE_BINARY_DIR}/include")
    add_executable(${test_name} ${test} ${MANAPIHTTP_TEST_TOOL_FILES})
    target_link_libraries(${test_name} manapihttp)
    add_test(${test_name} ${test_name})
    set(last_test_name ${test_name})
endforeach()

ADD_CUSTOM_TARGET(${last_test_name}_mkdir ALL COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${last_test_name}>)
ADD_CUSTOM_TARGET(${last_test_name}_link_target ALL COMMAND ${CMAKE_COMMAND} -E create_symlink
        "$<TARGET_FILE:manapihttp>" "$<TARGET_FILE_DIR:${last_test_name}>/$<TARGET_FILE_NAME:manapihttp>"
        DEPENDS ${last_test_name}_mkdir)