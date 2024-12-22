cmake_minimum_required(VERSION 3.9)
project(manapihttp VERSION 0.0.1 DESCRIPTION "Fast HTTP Server/Client Library for easy develop API-Services and Web Applications")

cmake_policy(SET CMP0070 NEW)

if (NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
else ()
    message(STATUS "This project is a top-level one")
endif ()

OPTION(MANAPIHTTP_JSON_DEBUG "JSON debug variables" OFF)
OPTION(MANAPIHTTP_QUICHE_DEPENDENCY "The QUIC Implementation by Cloudflare" OFF)
OPTION(MANAPIHTTP_MSQUIC_DEPENDENCY "The QUIC Implementation by Microsoft" OFF)
OPTION(MANAPIHTTP_OPENSSL_DEPENDENCY "TLS, QUIC and etc implementations by OpenSSL" OFF)
OPTION(MANAPIHTTP_WOLFSSL_DEPENDENCY "TLS, QUIC and etc implementations by WolfSSL" OFF)

OPTION(CMAKE_BUILD_TYPE "Library build type" "Release")

message(STATUS "The QUIC Implementation by Cloudflare: ${MANAPIHTTP_QUICHE_DEPENDENCY}")
message(STATUS "The QUIC Implementation by Microsoft: ${MANAPIHTTP_MSQUIC_DEPENDENCY}")
message(STATUS "TLS, QUIC and etc implementations by OpenSSL: ${MANAPIHTTP_OPENSSL_DEPENDENCY}")
message(STATUS "TLS, QUIC and etc implementations by WolfSSL: ${MANAPIHTTP_WOLFSSL_DEPENDENCY}")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Build type: Debug")
    add_compile_definitions(MANAPIHTTP_HTTP_BUILD_DEBUG)
endif ()

if (MANAPIHTTP_BUILD_METHOD STREQUAL "conan")
    message(STATUS "Build method: conan")

    find_package(gmp REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(libev REQUIRED)
    find_package(CURL REQUIRED)

    if (MANAPIHTTP_QUICHE_DEPENDENCY)
        find_package(quiche REQUIRED)
    endif ()

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        find_package(wolfssl REQUIRED)
    endif ()

    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        find_package(OpenSSL REQUIRED)
    endif ()
else ()
    message(STATUS "Build method: default")

    find_package(PkgConfig REQUIRED)

    # include finders
    include(./cmake/FindLibev.cmake)
    include(./cmake/FindLsQPack.cmake)

    pkg_check_modules(GMP REQUIRED gmp)
    pkg_check_modules(ZLIB REQUIRED zlib)
    pkg_check_modules(CURL REQUIRED libcurl)

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        pkg_check_modules(WOLFSSL REQUIRED wolfssl)
    endif ()

    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        pkg_check_modules(OPENSSL REQUIRED openssl)
    endif ()

    if (MANAPIHTTP_QUICHE_DEPENDENCY)
        pkg_check_modules(QUIC REQUIRED quiche)
    endif ()

    if (MANAPIHTTP_MSQUIC_DEPENDENCY)
        include(./cmake/FindMsQuic.cmake)
    endif ()
endif ()


# Preprocesses
if (MANAPIHTTP_JSON_DEBUG)
    set(MANAPIHTTP_JSON_DEBUG "true")
else ()
    set(MANAPIHTTP_JSON_DEBUG "false")
endif ()

file(READ preprocess/ManapiParams.hpp TEXT)
string(REPLACE "{{ VAR_MANAPIHTTP_JSON_DEBUG }}" ${MANAPIHTTP_JSON_DEBUG} TEXT "${TEXT}")
file(GENERATE OUTPUT include/ManapiParams.hpp CONTENT "${TEXT}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS include/ManapiParams.hpp)

add_compile_definitions(MANAPIHTTP_OPENSSL_DEPENDENCY=$<BOOL:${MANAPIHTTP_OPENSSL_DEPENDENCY}> MANAPIHTTP_WOLFSSL_DEPENDENCY=$<BOOL:${MANAPIHTTP_WOLFSSL_DEPENDENCY}>)

set(CMAKE_CXX_STANDARD 23)

set(MANAPIHTTP_HTTP_FILES
        include/ManapiHttp.hpp
        include/services/ManapiThreadPool.hpp
        include/services/ManapiTask.hpp
        include/ManapiUtils.hpp
        include/ManapiHttpResponse.hpp
        include/ManapiHttpRequest.hpp
        include/ManapiBigint.hpp
        include/components/ManapiThreadSafe.hpp
        include/ManapiHttpTypes.hpp
        include/ManapiFilesystem.hpp
        include/ManapiBase64.hpp
        include/compress/ManapiCompress.hpp
        include/services/ManapiTaskFunction.hpp
        include/ManapiHash.hpp
        include/services/ManapiFetch.hpp
        include/services/ManapiTimerPool.hpp
        include/ManapiHttpPool.hpp
        include/ManapiJsonMask.hpp
        include/ManapiSite.hpp
        include/ManapiSite.hpp
        include/ManapiHttpConfig.hpp
        include/extensions/ManapiJsonWebTokenTraits.hpp
        include/extensions/ManapiJsonWebTokenDefaults.hpp
        include/ManapiUnicode.hpp
        include/ManapiJsonBuilder.hpp
        include/ManapiBeforeDelete.hpp
        include/worker/Base.hpp
        include/http/Base.hpp
        include/worker/OpenSSL_TLS.hpp
        include/http/HTTPv1_1.hpp
        include/http/HTTPv1_1.hpp
        include/http/HeaderView.hpp
        include/compress/ManapiHPack.hpp
        include/worker/HTTPv2.hpp
        include/components/SmartBuffer.hpp
        include/components/ConnectionsStorage.hpp
        include/ManapiHttpMime.hpp
        include/components/Atomic.hpp
        include/worker/QUIC.hpp
        include/worker/TCP.hpp
        include/ManapiJson.hpp
        ${CMAKE_CURRENT_BINARY_DIR}/include/ManapiParams.hpp

        src/ManapiHttp.cpp
        src/services/ManapiThreadPool.cpp
        src/services/ManapiTask.cpp
        src/ManapiUtils.cpp
        src/ManapiHttpResponse.cpp
        src/ManapiHttpRequest.cpp
        src/ManapiJson.cpp
        src/ManapiBigint.cpp
        src/ManapiCompress.cpp
        src/ManapiFilesystem.cpp
        src/ManapiHash.cpp
        src/services/ManapiTaskFunction.cpp
        src/services/ManapiFetch.cpp
        src/ManapiJsonMask.cpp
        src/ManapiHttpPool.cpp
        src/services/ManapiTimerPool.cpp
        src/ManapiSite.cpp
        src/ManapiHttpConfig.cpp
        src/ManapiUnicode.cpp
        src/ManapiJsonBuilder.cpp
        src/ManapiBeforeDelete.cpp
        src/worker/Base.cpp
        src/http/Base.cpp
        src/worker/OpenSSL_TLS.cpp
        src/http/HTTPv1_1.cpp
        src/http/HTTPv2.cpp
        src/http/HeaderView.cpp
        src/worker/HTTPv2.cpp
        src/components/SmartBuffer.cpp
        src/ManapiHttpMime.cpp
        src/worker/QUIC.cpp
        src/worker/TCP.cpp
        src/components/FormData.cpp
        include/components/FormData.hpp
        src/ManapiAsync.cpp
        include/ManapiAsync.hpp
        src/services/ManapiFutureTask.cpp
        include/services/ManapiFutureTask.hpp
        include/async/ManapiAsyncMutex.hpp
        include/async/ManapiAsyncConditionVariable.hpp)

if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
    # EXE
    message(STATUS "Build type 'exe'")

    add_compile_definitions(MANAPIHTTP_HTTP_AS_EXECUTABLE)
    add_executable(${PROJECT_NAME} main.cpp ${MANAPIHTTP_HTTP_FILES})
else ()
    # LIB
    message(STATUS "Build type 'lib'")

    add_compile_definitions(MANAPIHTTP_HTTP_AS_LIBRARY)
    add_library(${PROJECT_NAME} ${MANAPIHTTP_HTTP_FILES})

    set_target_properties(${PROJECT_NAME} PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR})

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PROJECT_NAME}.pc.in ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc @ONLY)
endif ()

set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${PROJECT_VERSION})

if (MANAPIHTTP_BUILD_METHOD STREQUAL "conan")
    target_link_libraries       (${PROJECT_NAME} ${QUIC_LIBRARIES} gmp::gmp ZLIB::ZLIB libev::libev CURL::libcurl)
    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} openssl::openssl)
    endif ()

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} wolfssl::wolfssl)
    endif ()

    if (MANAPIHTTP_QUICHE_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} quiche::quiche)
    endif ()
else ()
    target_link_libraries       (${PROJECT_NAME} PUBLIC ${GMP_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBEV_LIBRARIES} ${LSQPACK_LIBRARIES} ${CURL_LIBRARIES})
    target_include_directories  (${PROJECT_NAME} PUBLIC ${GMP_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} ${LIBEV_INCLUDE_DIRS} ${LSQPACK_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})

    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${OPENSSL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${OPENSSL_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${WOLFSSL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${WOLFSSL_INCLUDE_DIRS})
    endif ()


    if (MANAPIHTTP_QUICHE_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${QUIC_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${QUIC_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_MSQUIC_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${MSQUIC_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${MSQUIC_INCLUDE_DIRS})
    endif ()

endif ()

# Include public headers from the folder
target_include_directories  (${PROJECT_NAME} PRIVATE include)
target_include_directories  (${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/include)

if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
    # nothing
else ()
    message(STATUS "OK")
    # If we want to install library
    target_compile_options(${PROJECT_NAME} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/Zi>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ggdb>
    )

    include(GNUInstallDirs)

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

    install(TARGETS ${PROJECT_NAME} EXPORT ManapiHttpConfig
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
    install(DIRECTORY ${CMAKE_BINARY_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})

    install(EXPORT ManapiHttpConfig DESTINATION lib/cmake/${PROJECT_NAME})

    set_target_properties(${PROJECT_NAME} PROPERTIES
            COMPILE_PDB_NAME ${PROJECT_NAME}
            COMPILE_PDB_OUTPUT_DIRECTORY ${CMAKE_INSTALL_BINDIR}
    )

    export(TARGETS ${PROJECT_NAME} FILE ManapiHttpConfig.cmake)
endif ()