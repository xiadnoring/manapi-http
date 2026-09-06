find_path(QUICHE_INCLUDE_DIRS
        NAMES quiche.h
        HINTS
            $ENV{HOME}/tmp/quiche/quiche/include
)
find_library(QUICHE_LIBRARIES
        NAMES quiche
        HINTS
            $ENV{HOME}/tmp/quiche/target/release
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(quiche REQUIRED_VARS
        QUICHE_LIBRARIES QUICHE_INCLUDE_DIRS)

mark_as_advanced(QUICHE_INCLUDE_DIRS QUICHE_LIBRARIES)

if (QUICHE_FOUND)
    set(TEST_PROGRAM
        "#include <quiche.h>
         #include <stdio.h>
         int main() {
             const char *version = quiche_version();
             printf(\"%s\\n\", version);
             return 0;
         }"
    )

    file(WRITE "${CMAKE_BINARY_DIR}/test_quiche_version.cpp" "${TEST_PROGRAM}")

    try_run(RUN_RESULT COMPILE_RESULT
            "${CMAKE_BINARY_DIR}"
            "${CMAKE_BINARY_DIR}/test_quiche_version.cpp"
            CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${QUICHE_INCLUDE_DIRS}"
            LINK_LIBRARIES ${QUICHE_LIBRARIES}
            RUN_OUTPUT_VARIABLE QUICHE_VERSION_RUNTIME
            COMPILE_OUTPUT_VARIABLE COMPILE_OUTPUT
    )

    if(COMPILE_RESULT AND RUN_RESULT EQUAL 0)
        string(STRIP ${QUICHE_VERSION_RUNTIME} QUICHE_VERSION_RUNTIME)
        set(QUICHE_VERSION ${QUICHE_VERSION_RUNTIME})
    endif()
endif ()