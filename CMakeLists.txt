cmake_minimum_required(VERSION 3.10)
project(manapihttp VERSION 0.0.2 DESCRIPTION "Fast HTTP Server/Client Library for easy develop API-Services and Web Applications")

cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0070 NEW)
cmake_policy(SET CMP0135 NEW)

if (EXISTS "./build/generators/conan_toolchain.cmake")
    include(./build/generators/conan_toolchain.cmake)
endif ()

add_compile_options(-fmerge-all-constants -fuse-ld=gold)

if (NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
else ()
    message(STATUS "This project is a top-level one")
endif ()

OPTION(BUILD_SHARED_LIBS "Build shared libs" OFF)
OPTION(MANAPIHTTP_GMP_DEPENDENCY "BigInt support by GMP" ON)
OPTION(MANAPIHTTP_CURL_DEPENDENCY "Fetch support by CURL" ON)
OPTION(MANAPIHTTP_ZLIB_DEPENDENCY "Some compresses support by zlib" ON)
OPTION(MANAPIHTTP_JSON_DEBUG "JSON debug variables" OFF)
OPTION(MANAPIHTTP_QUICHE_DEPENDENCY "The QUIC Implementation by Cloudflare" ON)
OPTION(MANAPIHTTP_TQUIC_DEPENDENCY "The QUIC Implementation by Tencent" ON)
OPTION(MANAPIHTTP_MSQUIC_DEPENDENCY "The QUIC Implementation by Microsoft" ON)
OPTION(MANAPIHTTP_OPENSSL_DEPENDENCY "TLS, QUIC and etc implementations by OpenSSL" ON)
OPTION(MANAPIHTTP_WOLFSSL_DEPENDENCY "TLS, QUIC and etc implementations by WolfSSL" ON)
OPTION(MANAPIHTTP_GRPC_DEPENDENCY "gPRC implementations by Google" OFF)
OPTION(MANAPIHTTP_BROTLI_DEPENDENCY "Brotli Enabled" OFF)
OPTION(MANAPIHTTP_ZSTD_DEPENDENCY "ZSTD Enabled" OFF)
OPTION(MANAPIHTTP_BUILD_METHOD "Build method" "default")
OPTION(MANAPIHTTP_WOLFSSL_WITH_ALPN "WolfSSL with ALPN" ON)
OPTION(MANAPIHTTP_NGHTTP2_DEPENDENCY "libnghttp2 by nghttp2" OFF)
OPTION(MANAPIHTTP_NGHTTP3_DEPENDENCY "libnghttp3 by nghttp3" OFF)
OPTION(MANAPIHTTP_CPPTRACE_DEPENDENCY "Stack Tracing c++" OFF)
OPTION(MANAPIHTTP_DEPENDENCY_FETCH_SHARED "Dependency using Fetch Enabled Shared Build" ON)

OPTION(MANAPIHTTP_OPENSSL_SYSTEM_DISABLE "Disable default OpenSSL" OFF)
OPTION(MANAPIHTTP_CURL_SYSTEM_DISABLE "Disable default cURL" OFF)
OPTION(MANAPIHTTP_NGHTTP2_SYSTEM_DISABLE "Disable default nghttp2" OFF)
OPTION(MANAPIHTTP_NGHTTP3_SYSTEM_DISABLE "Disable default nghttp3" OFF)

OPTION(CMAKE_BUILD_TYPE "Library build type" "Release")
message(STATUS "C++ Stack Tracing: ${MANAPIHTTP_CPPTRACE_DEPENDENCY}")
message(STATUS "MANAPIHTTP_BUILD_METHOD: ${MANAPIHTTP_BUILD_METHOD}")
message(STATUS "gPRC implementations by Google: ${MANAPIHTTP_GRPC_DEPENDENCY}")
message(STATUS "The QUIC Implementation by Cloudflare: ${MANAPIHTTP_QUICHE_DEPENDENCY}")
message(STATUS "The QUIC Implementation by Tencent: ${MANAPIHTTP_TQUIC_DEPENDENCY}")
message(STATUS "The QUIC Implementation by Microsoft: ${MANAPIHTTP_MSQUIC_DEPENDENCY}")
message(STATUS "TLS, QUIC and etc implementations by OpenSSL: ${MANAPIHTTP_OPENSSL_DEPENDENCY}")
message(STATUS "TLS, QUIC and etc implementations by WolfSSL: ${MANAPIHTTP_WOLFSSL_DEPENDENCY}")
message(STATUS "BigInt support by GMP: ${MANAPIHTTP_GMP_DEPENDENCY}")
message(STATUS "Fetch support by CURL: ${MANAPIHTTP_CURL_DEPENDENCY}")
message(STATUS "Some compresses support by zlib: ${MANAPIHTTP_ZLIB_DEPENDENCY}")
message(STATUS "libnghttp2 by nghttp2" ${MANAPIHTTP_NGHTTP2_DEPENDENCY})
message(STATUS "libnghttp3 by nghttp3" ${MANAPIHTTP_NGHTTP3_DEPENDENCY})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/shm>")
set(MANAPIHTTP_GMP_DEPENDENCY_OBJ OFF)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Build type: Debug")
    add_compile_definitions(MANAPIHTTP_HTTP_BUILD_DEBUG)
endif ()

if (MANAPIHTTP_BUILD_METHOD STREQUAL "conan")
    message(STATUS "Build method: conan")

    find_package(libuv REQUIRED)

    if (MANAPIHTTP_GMP_DEPENDENCY)
        find_package(gmp REQUIRED)
    endif ()

    if (MANAPIHTTP_ZLIB_DEPENDENCY)
        find_package(ZLIB REQUIRED)
    endif ()

    if (MANAPIHTTP_CURL_DEPENDENCY)
        find_package(CURL REQUIRED)
    endif ()

    if (MANAPIHTTP_QUICHE_DEPENDENCY)
        find_package(quiche REQUIRED)
        add_compile_definitions(MANAPIHTTP_QUICHE_VERSION="${quiche_VERSION}")
    endif ()

    if (MANAPIHTTP_TQUIC_DEPENDENCY)
        find_package(tquic REQUIRED)
    endif ()

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        find_package(wolfssl REQUIRED)
    endif ()

    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        find_package(OpenSSL REQUIRED)
    endif ()

    if (MANAPIHTTP_BROTLI_DEPENDENCY)
        find_package(brotli REQUIRED)
    endif ()

    if (MANAPIHTTP_ZSTD_DEPENDENCY)
        find_package(zstd REQUIRED)
    endif ()

    if (MANAPIHTTP_GRPC_DEPENDENCY)
        find_package(protobuf REQUIRED)
        find_package(gRPC REQUIRED)
    endif ()

    if (MANAPIHTTP_NGHTTP2_DEPENDENCY)
        find_package(libnghttp2 REQUIRED)
    endif ()

    if (MANAPIHTTP_NGHTTP3_DEPENDENCY)
        find_package(nghttp3 REQUIRED)
    endif ()

    if (MANAPIHTTP_CPPTRACE_DEPENDENCY)
        find_package(cpptrace REQUIRED)
    endif ()

    if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
        # For Testing
        find_package(PostgreSQL REQUIRED)
    endif ()
else ()
    include(FetchContent)
    include(ExternalProject)

    set(MANAPIHTTP_DEPS_PREFIX "${CMAKE_BINARY_DIR}/deps")
    set(MANAPIHTTP_PATCHES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches")

    message(STATUS "Build method: ${MANAPIHTTP_BUILD_METHOD}")

    set(MANAPIHTTP_DEPENDENCY_FETCH OFF)

    if (MANAPIHTTP_BUILD_METHOD STREQUAL "fetch")
        set(MANAPIHTTP_DEPENDENCY_FETCH ON)
    endif ()

    find_package(PkgConfig)

    # include finders
    # include(./cmake/FindLibev.cmake)

    if (MANAPIHTTP_DEPENDENCY_FETCH)
        pkg_check_modules(LIBUV REQUIRED IMPORTED_TARGET libuv)
    else ()
        pkg_check_modules(LIBUV IMPORTED_TARGET libuv)
    endif ()
    # set (LIBUV_FOUND OFF)
    if (NOT LIBUV_FOUND)
        if (MANAPIHTTP_DEPENDENCY_FETCH)
            set(MANAPIHTTP_LIBUV_INSTALL_DIR ${MANAPIHTTP_DEPS_PREFIX}/lib/libuv)
            if (MANAPIHTTP_DEPENDENCY_FETCH_SHARED)
                set(LIBUV_LIBRARIES "${MANAPIHTTP_LIBUV_INSTALL_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}uv${CMAKE_SHARED_LIBRARY_SUFFIX}")
            else ()
                set(LIBUV_LIBRARIES "${MANAPIHTTP_LIBUV_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}uv${CMAKE_STATIC_LIBRARY_SUFFIX}")
            endif ()
            set(LIBUV_INCLUDE_DIRS $<BUILD_INTERFACE:${MANAPIHTTP_LIBUV_INSTALL_DIR}/include>)

            ExternalProject_Add(
                    libuv
                    PREFIX "${MANAPIHTTP_DEPS_PREFIX}"
                    DOWNLOAD_NAME libuv.tar.gz
                    GIT_REPOSITORY https://github.com/libuv/libuv
                    GIT_TAG v1.51.0
                    CMAKE_ARGS
                        -DCMAKE_INSTALL_PREFIX:PATH=${MANAPIHTTP_LIBUV_INSTALL_DIR}
                        -DBUILD_SHARED_LIBS=${MANAPIHTTP_DEPENDENCY_FETCH_SHARED}
                    BUILD_BYPRODUCTS "${LIBUV_LIBRARIES}"
            )
            set(MANAPIHTTP_LIBUV_DEPENDENCY_FETCH ON)
        endif ()
    endif ()

    if (MANAPIHTTP_GMP_DEPENDENCY)
        pkg_check_modules(GMP gmp)
        # set(GMP_FOUND OFF)

        if (NOT GMP_FOUND)
            if (MANAPIHTTP_DEPENDENCY_FETCH)
                set(MANAPIHTTP_GMP_INSTALL_DIR ${MANAPIHTTP_DEPS_PREFIX}/lib/gmp)
                if (MANAPIHTTP_DEPENDENCY_FETCH_SHARED)
                    set(GMP_LIBRARIES "${MANAPIHTTP_GMP_INSTALL_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}gmp${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(GMP_LIBRARIES "${MANAPIHTTP_GMP_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}gmp${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()
                set(GMP_INCLUDE_DIRS $<BUILD_INTERFACE:${MANAPIHTTP_GMP_INSTALL_DIR}/include>)

                ExternalProject_Add(
                    gmp
                    PREFIX "${MANAPIHTTP_DEPS_PREFIX}"
                    DOWNLOAD_NAME gmp.tar.gz
                    URL https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.bz2 https://gmplib.org/download/gmp/gmp-6.3.0.tar.bz2
                    URL_HASH SHA256=ac28211a7cfb609bae2e2c8d6058d66c8fe96434f740cf6fe2e47b000d1c20cb
                    USES_TERMINAL_DOWNLOAD TRUE
                    PATCH_COMMAND patch -p1 --directory "${MANAPIHTTP_DEPS_PREFIX}/src/gmp" < "${MANAPIHTTP_PATCHES_DIR}/gmp-3.5.0-0001-configure.patch"
                    CONFIGURE_COMMAND sh ./../gmp/configure --prefix=${MANAPIHTTP_GMP_INSTALL_DIR}
                        $<$<BOOL:${MANAPIHTTP_DEPENDENCY_FETCH_SHARED}>:--enable-shared=True>
                        $<$<BOOL:${MANAPIHTTP_DEPENDENCY_FETCH_SHARED}>:--enable-static=False>
                        $<$<NOT:$<BOOL:${MANAPIHTTP_DEPENDENCY_FETCH_SHARED}>>:--enable-shared=False>
                        $<$<NOT:$<BOOL:${MANAPIHTTP_DEPENDENCY_FETCH_SHARED}>>:--enable-static=True>
                    BUILD_COMMAND make
                    TEST_COMMAND ""
                    INSTALL_COMMAND make install
                    INSTALL_DIR "${MANAPIHTTP_GMP_INSTALL_DIR}"
                    BUILD_BYPRODUCTS "${GMP_LIBRARIES}"
                )
                set(MANAPIHTTP_GMP_DEPENDENCY_FETCH ON)
            else ()
                set(MANAPIHTTP_GMP_DEPENDENCY OFF)
            endif ()
        endif ()
    endif ()

    if (MANAPIHTTP_ZLIB_DEPENDENCY)
        pkg_check_modules(ZLIB zlib)
        #set(ZLIB_FOUND OFF)
        if (NOT ZLIB_FOUND)
            if (MANAPIHTTP_DEPENDENCY_FETCH)
                set(MANAPIHTTP_ZLIB_INSTALL_DIR ${MANAPIHTTP_DEPS_PREFIX}/lib/zlib)
                if (MANAPIHTTP_DEPENDENCY_FETCH_SHARED)
                    set(ZLIB_LIBRARIES "${MANAPIHTTP_ZLIB_INSTALL_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}z${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(ZLIB_LIBRARIES "${MANAPIHTTP_ZLIB_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}z${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()
                set(ZLIB_INCLUDE_DIRS $<BUILD_INTERFACE:${MANAPIHTTP_ZLIB_INSTALL_DIR}/include>)

                ExternalProject_Add(
                        zlib
                        PREFIX "${MANAPIHTTP_DEPS_PREFIX}"
                        DOWNLOAD_NAME zlib.tar.gz
                        URL https://zlib.net/fossils/zlib-1.3.1.tar.gz https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
                        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
                        CMAKE_ARGS
                            -DCMAKE_INSTALL_PREFIX:PATH=${MANAPIHTTP_ZLIB_INSTALL_DIR}
                            -DZLIB_BUILD_EXAMPLES=OFF
                            -DSKIP_INSTALL_ALL=OFF
                            -DSKIP_INSTALL_LIBRARIES=OFF
                            -DSKIP_INSTALL_HEADERS=OFF
                            -DSKIP_INSTALL_FILES=ON
                            -DBUILD_SHARED_LIBS=${MANAPIHTTP_DEPENDENCY_FETCH_SHARED}
                        BUILD_BYPRODUCTS "${ZLIB_LIBRARIES}"
                )
                set(MANAPIHTTP_ZLIB_DEPENDENCY_FETCH ON)
            else ()
                set(MANAPIHTTP_ZLIB_DEPENDENCY OFF)
            endif ()
        endif ()
    endif ()

    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        if (MANAPIHTTP_OPENSSL_SYSTEM_DISABLE)
            set(OPENSSL_FOUND OFF)
        else ()
            pkg_check_modules(OPENSSL openssl)
        endif ()
        if (NOT OPENSSL_FOUND)
            if (MANAPIHTTP_DEPENDENCY_FETCH)
                set(MANAPIHTTP_OPENSSL_INSTALL_DIR ${MANAPIHTTP_DEPS_PREFIX}/lib/openssl)
                if (MANAPIHTTP_DEPENDENCY_FETCH_SHARED)
                    set(OPENSSL_LIBRARIES "${MANAPIHTTP_OPENSSL_INSTALL_DIR}/lib64/${CMAKE_SHARED_LIBRARY_PREFIX}crypto${CMAKE_SHARED_LIBRARY_SUFFIX};${MANAPIHTTP_OPENSSL_INSTALL_DIR}/lib64/${CMAKE_SHARED_LIBRARY_PREFIX}ssl${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(OPENSSL_LIBRARIES "${MANAPIHTTP_OPENSSL_INSTALL_DIR}/lib64/${CMAKE_STATIC_LIBRARY_PREFIX}crypto${CMAKE_STATIC_LIBRARY_SUFFIX};${MANAPIHTTP_OPENSSL_INSTALL_DIR}/lib64/${CMAKE_STATIC_LIBRARY_PREFIX}ssl${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()

                set(OPENSSL_INCLUDE_DIRS $<BUILD_INTERFACE:${MANAPIHTTP_OPENSSL_INSTALL_DIR}/include>)

                ExternalProject_Add(
                    openssl
                    PREFIX "${MANAPIHTTP_DEPS_PREFIX}"
                    DOWNLOAD_NAME openssl.tar.gz
                    URL https://github.com/openssl/openssl/archive/refs/tags/openssl-3.5.2.zip
                    URL_HASH SHA256=ea744bf784bdc3554284f6dc82d40e82cb89dd86c737151b2890289b6624c180
                    USES_TERMINAL_DOWNLOAD TRUE
                    CONFIGURE_COMMAND
                        ${MANAPIHTTP_DEPS_PREFIX}/src/openssl/config
                        --prefix=${MANAPIHTTP_OPENSSL_INSTALL_DIR}
                        --openssldir=${MANAPIHTTP_OPENSSL_INSTALL_DIR}
                    BUILD_COMMAND make
                    INSTALL_COMMAND make install
                    INSTALL_DIR ${MANAPIHTTP_OPENSSL_INSTALL_DIR}
                    BUILD_BYPRODUCTS "${OPENSSL_LIBRARIES}"
                )

                set(MANAPIHTTP_OPENSSL_DEPENDENCY_FETCH ON)
            else ()
                set(MANAPIHTTP_OPENSSL_DEPENDENCY OFF)
            endif ()
        endif ()
    endif ()

    if (MANAPIHTTP_CURL_DEPENDENCY)
        if (MANAPIHTTP_CURL_SYSTEM_DISABLE)
            set(CURL_FOUND OFF)
        else ()
            pkg_check_modules(CURL libcurl)
        endif ()
        # set(CURL_FOUND OFF)
        if (NOT CURL_FOUND)
            if (MANAPIHTTP_DEPENDENCY_FETCH)
                set(MANAPIHTTP_CURL_INSTALL_DIR ${MANAPIHTTP_DEPS_PREFIX}/lib/curl)
                if (MANAPIHTTP_DEPENDENCY_FETCH_SHARED)
                    set(CURL_LIBRARIES "${MANAPIHTTP_CURL_INSTALL_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}curl${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(CURL_LIBRARIES "${MANAPIHTTP_CURL_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}curl${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()
                set(CURL_INCLUDE_DIRS $<BUILD_INTERFACE:${MANAPIHTTP_CURL_INSTALL_DIR}/include>)

                ExternalProject_Add(
                        curl
                        PREFIX "${MANAPIHTTP_DEPS_PREFIX}"
                        DOWNLOAD_NAME curl.tar.gz
                        GIT_REPOSITORY https://github.com/curl/curl
                        GIT_TAG curl-8_15_0
                        CMAKE_ARGS
                            -DCMAKE_INSTALL_PREFIX:PATH=${MANAPIHTTP_CURL_INSTALL_DIR}
                            -DBUILD_SHARED_LIBS=${MANAPIHTTP_DEPENDENCY_FETCH_SHARED}
                        BUILD_BYPRODUCTS "${CURL_LIBRARIES}"
                )
                set(MANAPIHTTP_CURL_DEPENDENCY_FETCH ON)
            else ()
                set(MANAPIHTTP_CURL_DEPENDENCY OFF)
            endif ()
        endif ()
    endif ()

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        pkg_check_modules(WOLFSSL wolfssl)
        if (NOT WOLFSSL_FOUND)
            set(MANAPIHTTP_WOLFSSL_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_TQUIC_DEPENDENCY)
        pkg_check_modules(TQUIC tquic)
        if (NOT TQUIC_FOUND)
            set(MANAPIHTTP_TQUIC_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_QUICHE_DEPENDENCY)
        pkg_check_modules(QUICHE quiche)
        if (NOT QUICHE_FOUND)
            set(MANAPIHTTP_QUICHE_DEPENDENCY OFF)
        else ()
            add_compile_definitions(MANAPIHTTP_QUICHE_VERSION="${QUICHE_VERSION}")
        endif ()
    endif ()

    if (MANAPIHTTP_MSQUIC_DEPENDENCY)
        include(./cmake/FindMsQuic.cmake)
        if (NOT MSQUIC_FOUND)
            set(MANAPIHTTP_MSQUIC_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_BROTLI_DEPENDENCY)
        include(./cmake/FindBrotli.cmake)
        if (NOT BROTLI_FOUND)
            set(MANAPIHTTP_BROTLI_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_GRPC_DEPENDENCY)
        pkg_search_module(GRPC grpc)
        pkg_search_module(GRPCPP grpc++>=1.22.0)
        pkg_check_modules(PROTOBUF protobuf)
        message(STATUS "grpc found: ${GRPC_FOUND}")
        message(STATUS "protobuf found: ${PROTOBUF_FOUND}")
        message(STATUS "grpcpp found: ${GRPCPP_FOUND}")
        if (NOT (GRPC_FOUND AND PROTOBUF_FOUND AND GRPCPP_FOUND))
            set(MANAPIHTTP_GRPC_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_ZSTD_DEPENDENCY)
        include(./cmake/FindZSTD.cmake)
        if (NOT ZSTD_FOUND)
            set(MANAPIHTTP_ZSTD_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_CPPTRACE_DEPENDENCY)
        include(./cmake/FindCppTrace.cmake)
        if (NOT CPPTRACE_FOUND)
            set(MANAPIHTTP_CPPTRACE_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_NGHTTP2_DEPENDENCY)
        if (MANAPIHTTP_NGHTTP2_SYSTEM_DISABLE)
            set(LIBNGHTTP2_FOUND OFF)
        else ()
            include(./cmake/FindLibNgHttp2.cmake)
        endif ()
        # set(LIBNGHTTP2_FOUND OFF)
        if (NOT LIBNGHTTP2_FOUND)
            if (MANAPIHTTP_DEPENDENCY_FETCH)
                set(MANAPIHTTP_LIBNGHTTP2_CMAKE_FLAGS "")
                if (NOT WIN32 AND NOT MANAPIHTTP_DEPENDENCY_FETCH_SHARED AND BUILD_SHARED_LIBS)
                    set(MANAPIHTTP_LIBNGHTTP2_CMAKE_FLAGS "${MANAPIHTTP_LIBNGHTTP2_CMAKE_FLAGS} -DCMAKE_POSITION_INDEPENDENT_CODE=ON")
                endif ()
                set(MANAPIHTTP_LIBNGHTTP2_INSTALL_DIR ${MANAPIHTTP_DEPS_PREFIX}/lib/libnghttp2)
                if (MANAPIHTTP_DEPENDENCY_FETCH_SHARED)
                    set(LIBNGHTTP2_LIBRARIES "${MANAPIHTTP_LIBNGHTTP2_INSTALL_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}nghttp2${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(LIBNGHTTP2_LIBRARIES "${MANAPIHTTP_LIBNGHTTP2_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}nghttp2${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()
                set(LIBNGHTTP2_INCLUDE_DIRS $<BUILD_INTERFACE:${MANAPIHTTP_LIBNGHTTP2_INSTALL_DIR}/include>)

                ExternalProject_Add(
                        libnghttp2
                        PREFIX "${MANAPIHTTP_DEPS_PREFIX}"
                        GIT_REPOSITORY https://github.com/nghttp2/nghttp2
                        GIT_TAG v1.66.0
                        CMAKE_ARGS
                            -DCMAKE_INSTALL_PREFIX:PATH=${MANAPIHTTP_LIBNGHTTP2_INSTALL_DIR}
                            -DBUILD_SHARED_LIBS=${MANAPIHTTP_DEPENDENCY_FETCH_SHARED}
                            ${MANAPIHTTP_LIBNGHTTP2_CMAKE_FLAGS}
                        BUILD_BYPRODUCTS "${LIBNGHTTP2_LIBRARIES}"
                )
                set(MANAPIHTTP_NGHTTP2_DEPENDENCY_FETCH ON)
            else ()
                set(MANAPIHTTP_NGHTTP2_DEPENDENCY OFF)
            endif ()
        endif ()
    endif ()

    if (MANAPIHTTP_NGHTTP3_DEPENDENCY)

        if (MANAPIHTTP_NGHTTP3_SYSTEM_DISABLE)
            set(LIBNGHTTP3_FOUND OFF)
        else ()
            include(./cmake/FindLibNgHttp3.cmake)
        endif ()
        # set(LIBNGHTTP3_FOUND OFF)
        if (NOT LIBNGHTTP3_FOUND)
            if (MANAPIHTTP_DEPENDENCY_FETCH)
                set(MANAPIHTTP_LIBNGHTTP3_INSTALL_DIR ${MANAPIHTTP_DEPS_PREFIX}/lib/libnghttp3)
                if (MANAPIHTTP_DEPENDENCY_FETCH_SHARED)
                    set(LIBNGHTTP3_LIBRARIES "${MANAPIHTTP_LIBNGHTTP3_INSTALL_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}nghttp3${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(LIBNGHTTP3_LIBRARIES "${MANAPIHTTP_LIBNGHTTP3_INSTALL_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}nghttp3${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()

                set(MANAPIHTTP_LIBNGHTTP3_CMAKE_FLAGS "")
                if (NOT WIN32 AND NOT MANAPIHTTP_DEPENDENCY_FETCH_SHARED AND BUILD_SHARED_LIBS)
                    set(MANAPIHTTP_LIBNGHTTP3_CMAKE_FLAGS "${MANAPIHTTP_LIBNGHTTP3_CMAKE_FLAGS} -DCMAKE_POSITION_INDEPENDENT_CODE=ON")
                endif ()

                set(LIBNGHTTP3_INCLUDE_DIRS $<BUILD_INTERFACE:${MANAPIHTTP_LIBNGHTTP3_INSTALL_DIR}/include>)

                ExternalProject_Add(
                        libnghttp3
                        PREFIX "${MANAPIHTTP_DEPS_PREFIX}"
                        GIT_REPOSITORY https://github.com/ngtcp2/nghttp3
                        GIT_TAG v1.11.0
                        CMAKE_ARGS
                            -DCMAKE_INSTALL_PREFIX:PATH=${MANAPIHTTP_LIBNGHTTP3_INSTALL_DIR}
                            -DBUILD_SHARED_LIBS=${MANAPIHTTP_DEPENDENCY_FETCH_SHARED}
                            ${MANAPIHTTP_LIBNGHTTP3_CMAKE_FLAGS}
                        BUILD_BYPRODUCTS "${LIBNGHTTP3_LIBRARIES}"
                )
                set(MANAPIHTTP_NGHTTP3_DEPENDENCY_FETCH ON)
            else ()
                set(MANAPIHTTP_NGHTTP3_DEPENDENCY OFF)
            endif ()
        endif ()
    endif ()
endif ()


include(CheckLibraryExists)

file(READ preprocess/ManapiParams.hpp TEXT)
string(REPLACE "{{ VAR_MANAPIHTTP_NAME }}" "\"manapihttp\"" TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_VERSION }}" "\"${CMAKE_PROJECT_VERSION}\"" TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_JSON_DEBUG }}" $<BOOL:${MANAPIHTTP_JSON_DEBUG}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_OPENSSL_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_OPENSSL_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_WOLFSSL_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_WOLFSSL_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_QUICHE_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_QUICHE_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_TQUIC_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_TQUIC_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_ZLIB_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_ZLIB_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_CURL_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_CURL_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_GMP_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_GMP_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_MSQUIC_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_MSQUIC_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_WOLFSSL_WITH_ALPN }}" $<BOOL:${MANAPIHTTP_WOLFSSL_WITH_ALPN}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_WOLFSSL_WITH_ALPN }}" $<BOOL:${MANAPIHTTP_WOLFSSL_WITH_ALPN}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_WOLFSSL_WITH_ALPN }}" $<BOOL:${MANAPIHTTP_WOLFSSL_WITH_ALPN}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_BROTLI_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_BROTLI_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_GRPC_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_GRPC_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_ZSTD_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_ZSTD_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_NGHTTP2_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_NGHTTP2_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_NGHTTP3_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_NGHTTP3_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_CPPTRACE_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_CPPTRACE_DEPENDENCY}> TEXT "${TEXT}")

file(GENERATE OUTPUT include/ManapiParams.hpp CONTENT "${TEXT}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS include/ManapiParams.hpp)

set(CMAKE_CXX_STANDARD 23)

set(MANAPIHTTP_HTTP_FILES
        include/ManapiHttp.hpp
        include/services/ManapiThreadPool.hpp
        include/ManapiUtils.hpp
        include/ManapiHttpResponse.hpp
        include/ManapiHttpRequest.hpp
        include/ManapiBigint.hpp
        include/ManapiHttpTypes.hpp
        include/ManapiFilesystem.hpp
        include/crypto/ManapiBase64.hpp
        include/compress/ManapiCompress.hpp
        include/ManapiHash.hpp
        include/services/ManapiFetch.hpp
        include/services/ManapiTimerPool.hpp
        include/http/ManapiHttpPool.hpp
        include/ManapiJsonMask.hpp
        include/http/ManapiSite.hpp
        include/ManapiHttpConfig.hpp
        include/ext/ManapiJsonWebTokenTraits.hpp
        include/ext/ManapiJsonWebTokenDefaults.hpp
        include/encoding/ManapiUnicode.hpp
        include/ManapiJsonBuilder.hpp
        include/ManapiBeforeDelete.hpp
        include/worker/ManapiBaseWorker.hpp
        include/http/ManapiBaseHttp.hpp
        include/worker/ManapiOpenSslOverTcp.hpp
        src/include/http/ManapiHttp1.hpp
        src/include/http/ManapiHttp2.hpp
        include/compress/ManapiHPack.hpp
        src/include/http/ManapiHttp2Interface.hpp
        src/include/http/ManapiHttp1Interface.hpp
        include/ManapiHttpMime.hpp
        include/components/ManapiAtomic.hpp
        include/worker/ManapiTcp.hpp
        include/ManapiJson.hpp
        ${CMAKE_CURRENT_BINARY_DIR}/include/ManapiParams.hpp

        src/ManapiHttp.cpp
        src/services/ManapiThreadPool.cpp
        src/ManapiUtils.cpp
        src/ManapiHttpResponse.cpp
        src/ManapiHttpRequest.cpp
        src/ManapiJson.cpp
        src/ManapiBigint.cpp
        src/compress/ManapiCompress.cpp
        src/ManapiFilesystem.cpp
        src/ManapiHash.cpp
        src/services/ManapiFetch.cpp
        src/ManapiJsonMask.cpp
        src/http/ManapiHttpPool.cpp
        src/services/ManapiTimerPool.cpp
        src/http/ManapiSite.cpp
        src/ManapiHttpConfig.cpp
        src/ManapiUnicode.cpp
        src/ManapiJsonBuilder.cpp
        src/ManapiBeforeDelete.cpp
        src/worker/ManapiBaseWorker.cpp
        src/http/ManapiBaseHttp.cpp
        src/worker/ManapiOpenSslOverTcp.cpp
        src/http/ManapiHttp1.cpp
        src/http/ManapiHttp2.cpp
        src/http/ManapiHttp2Interface.cpp
        src/ManapiHttpMime.cpp
        src/worker/ManapiTcp.cpp
        src/components/ManapiFormData.cpp
        include/components/ManapiFormData.hpp
        src/ManapiAsync.cpp
        include/ManapiAsync.hpp
        include/async/ManapiAsyncMutex.hpp
        include/async/ManapiAsyncConditionVariable.hpp
        include/components/ManapiChain.hpp
        include/async/ManapiAsyncTimer.hpp
        include/crypto/ManapiAEAD.hpp
        include/crypto/ManapiHKDF.hpp
        include/crypto/ManapiCryptoUtils.hpp
        include/crypto/ManapiAES.hpp
        src/worker/ManapiUdp.cpp
        include/worker/ManapiUdp.hpp
        include/ManapiInitTools.hpp
        include/worker/ManapiHttp3Cloudflare.hpp
        src/worker/ManapiHttp3Cloudflare.cpp
        include/async/ManapiAsyncPromise.hpp
        include/encoding/ManapiURL.hpp
        include/ManapiErrors.hpp
        src/ManapiErrors.cpp
        include/ManapiDebug.hpp
        include/ManapiTime.hpp
        include/ManapiString.hpp
        src/ManapiString.cpp
        include/ManapiMath.hpp
        src/ManapiMath.cpp
        include/http/ManapiHttpUtils.hpp
        src/http/ManapiHttpUtils.cpp
        include/async/ManapiAsyncTools.hpp
        include/worker/ManapiWolfSslOverTcp.hpp
        src/worker/ManapiWolfSslOverTcp.cpp
        # include/components/ManapiAtomicAsync.hpp
        include/services/ManapiEventLoop.hpp
        src/services/ManapiEventLoop.cpp
        include/async/ManapiAsyncContext.hpp
        src/async/ManapiAsyncContext.cpp
        src/async/ManapiAsyncMutex.cpp
        src/async/ManapiAsyncConditionVariable.cpp
        include/async/ManapiAsyncFileStream.hpp
        src/async/ManapiAsyncFileStream.cpp
        include/ext/pq/AsyncPostgreClient.hpp
        include/async/ManapiAsyncSocket.hpp
        include/ext/pq/AsyncPostgreResult.hpp
        include/ext/pq/AsyncPostgreRow.hpp
        include/ext/pq/AsyncPostgreField.hpp
        include/ext/pq/AsyncPostgreValue.hpp
        include/ext/pq/AsyncPostgreValueTypes.hpp
        include/ext/pq/AsyncPostgreNotification.hpp
        include/async/ManapiAsyncParallelRun.hpp
        include/services/ManapiFetch2.hpp
        include/components/ManapiTimerObject.hpp
        src/components/ManapiTimerObject.cpp
        include/ext/jemallocator.hpp
        include/components/ManapiBuffer.hpp
        src/components/ManapiBuffer.cpp
        include/services/ManapiObjectPool.hpp
        src/ManapiInitTools.cpp
        include/components/ManapiFileTransferInfo.hpp
        src/async/ManapiAsyncSocket.cpp
        include/async/ManapiCancellation.hpp
        src/async/ManapiCancellation.cpp
        include/components/ManapiURLDecodeStream.hpp
        src/components/ManapiURLDecodeStream.cpp
        src/http/ManapiURLParams.cpp
        include/http/ManapiURLParams.hpp
        src/ManapiHttpTypes.cpp
        src/encoding/ManapiURL.cpp
        include/ManapiVersions.hpp
        src/worker/ManapiTlsOverTcp.cpp
        include/worker/ManapiTlsOverTcp.hpp
        src/async/ManapiAsyncLogger.cpp
        include/async/ManapiAsyncLogger.hpp
        include/components/ManapiEventStructures.hpp
        src/components/ManapiEventStructures.cpp
        src/include/ManapiWindows.hpp
        src/crypto/ManapiCryptoUtils.cpp
        src/include/ManapiHttpStructs.hpp
        src/include/ManapiDefaultErrors.hpp
        include/ManapiProcess.hpp
        src/ManapiProcess.cpp
        include/http/ManapiSiteCtx.hpp
        src/http/ManapiSiteCtx.cpp
        include/async/ManapiAsyncThreadsMutex.hpp
        src/async/ManapiAsyncThreadsMutex.cpp
        include/async/ManapiEasyCancellation.hpp
        src/async/ManapiEasyCancellation.cpp
        src/services/ManapiObjectPool.cpp
        include/worker/ManapiHttp2Worker.hpp
        src/worker/ManapiHttp2Worker.cpp
        src/http/ManapiHttp1Interface.cpp
        include/worker/ManapiInterfaceWorker.hpp
        src/worker/ManapiInterfaceWorker.cpp
        include/components/ManapiSlice.hpp
        src/components/ManapiSlice.cpp
        src/compress/ManapiHPack.cpp
        src/include/http/ManapiNgHttp2Interface.hpp
        src/http/ManapiNgHttp2Interface.cpp
        src/include/ManapiJsonMaskUtils.hpp
        src/include/ManapiDebug.hpp
        include/ManapiUtils.hpp
        src/crypto/ManapiAEAD.cpp
        src/crypto/ManapiHKDF.cpp
        src/crypto/ManapiAES.cpp
        include/components/ManapiMultithreadStorage.hpp
        src/components/ManapiMultithreadStorage.cpp
        include/services/ManapiGrpc.hpp
        src/services/ManapiGrpc.cpp
        include/components/ManapiConfig.hpp
        include/components/ManapiConfig.hpp
        src/components/ManapiConfig.cpp
        src/include/ManapiInternalGrpc.hpp
        include/services/ManapiDns.hpp
        src/services/ManapiDns.cpp
        src/ManapiTime.cpp
        src/crypto/ManapiBase64.cpp
        src/services/ManapiFetch2.cpp
        src/ManapiDebug.cpp
        src/include/ManapiEventStructuresInternal.hpp
        include/ext/ManapiMustache.hpp
        src/include/ManapiSiteInternal.hpp
        src/include/worker/ManapiQuicOpenSsl.hpp
        src/worker/ManapiQuicOpenSsl.cpp
        include/worker/ManapiBaseUtils.hpp
        src/http/ManapiNgHttp3Interface.cpp
        src/include/http/ManapiNgHttp3Interface.hpp
        include/worker/ManapiHttp3Worker.hpp
        src/worker/ManapiHttp3Worker.cpp
        src/worker/ManapiBaseUtils.cpp
        include/components/ManapiFunction.hpp
        include/ext/pq/AsyncPostgreError.hpp
        src/include/ManapiEventLoopCurl.hpp
)

if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe" OR MANAPIHTTP_BUILD_TYPE STREQUAL "test")
    # EXE
    message(STATUS "Build type 'exe'")

    if (MANAPIHTTP_GRPC_DEPENDENCY)
        if (MANAPIHTTP_BUILD_METHOD STREQUAL "conan")
            if (DEFINED protobuf_BIN_DIRS_DEBUG)
                set(Protobuf_PROTOC_EXECUTABLE ${protobuf_BIN_DIRS_DEBUG}/protoc)
            else ()
                set(Protobuf_PROTOC_EXECUTABLE ${protobuf_BIN_DIRS_RELEASE}/protoc)
            endif ()
        else ()
            find_program(Protobuf_PROTOC_EXECUTABLE protoc)
        endif ()

        set (PROTOS protobuf/helloworld.proto)

        foreach (FIL IN LISTS PROTOS)
            get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
            get_filename_component(FIL_WE ${FIL} NAME_WE)
            if(NOT PROTOBUF_GENERATE_CPP_APPEND_PATH)
                get_filename_component(FIL_DIR ${FIL} DIRECTORY)
                if(FIL_DIR)
                    set(FIL_WE "${FIL_DIR}/${FIL_WE}")
                endif()
            endif()

            list(APPEND PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.cc")
            list(APPEND PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.h")

            list(APPEND PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.pb.cc")
            list(APPEND PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.pb.h")

            if (DEFINED grpc_LIB_DIRS_DEBUG)
                set(MANAPI_grpc_LIB_DIRS ${grpc_LIB_DIRS_DEBUG})
            else ()
                set(MANAPI_grpc_LIB_DIRS ${grpc_LIB_DIRS_RELEASE})
            endif ()

            if (DEFINED protobuf_LIB_DIRS_DEBUG)
                set(MANAPI_protobuf_LIB_DIRS ${protobuf_LIB_DIRS_DEBUG})
            else ()
                set(MANAPI_protobuf_LIB_DIRS ${protobuf_LIB_DIRS_RELEASE})
            endif ()

            add_custom_command(
                    OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.pb.cc"
                            "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.pb.h"
                    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                    ARGS ${FIL} -I${CMAKE_SOURCE_DIR} --cpp_out=${CMAKE_BINARY_DIR}/generated
                    DEPENDS ${Protobuf_PROTOC_EXECUTABLE}
                    VERBATIM
            )


            set(GRPC_CPP_PLUGIN, "")

            if (MANAPIHTTP_BUILD_METHOD STREQUAL "conan")
                set(GRPC_CPP_PLUGIN, gRPC::grpc_cpp_plugin)
            else ()
                find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
            endif ()

            if (WIN32)
                add_custom_command(
                        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.cc"
                                "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.h"
                        COMMAND set "PATH=${MANAPI_grpc_LIB_DIRS};${MANAPI_protobuf_LIB_DIRS};%PATH%"
                        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                        ARGS ${FIL} -I${CMAKE_SOURCE_DIR} --grpc_out=${CMAKE_BINARY_DIR}/generated --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
                        DEPENDS ${Protobuf_PROTOC_EXECUTABLE}
                        VERBATIM
                )
            elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
                add_custom_command(
                        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.cc"
                                "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.h"
                        COMMAND export LD_LIBRARY_PATH=${MANAPI_grpc_LIB_DIRS}:${MANAPI_protobuf_LIB_DIRS}
                        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                        ARGS ${FIL} -I${CMAKE_SOURCE_DIR} --grpc_out=${CMAKE_BINARY_DIR}/generated --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
                        DEPENDS ${Protobuf_PROTOC_EXECUTABLE}
                        VERBATIM
                )
            else ()
                add_custom_command(
                        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.cc"
                                "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.h"
                        COMMAND LD_LIBRARY_PATH=${MANAPI_grpc_LIB_DIRS}:${MANAPI_protobuf_LIB_DIRS} ${Protobuf_PROTOC_EXECUTABLE}
                        ARGS ${FIL} -I${CMAKE_SOURCE_DIR} --grpc_out=${CMAKE_BINARY_DIR}/generated --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
                        DEPENDS ${Protobuf_PROTOC_EXECUTABLE}
                        VERBATIM
                )
            endif ()
        endforeach ()

#        foreach (X IN LISTS PROTOS)
#            execute_process(COMMAND bash -c
#                    "${Protobuf_PROTOC_EXECUTABLE} --cpp_out :${CMAKE_BINARY_DIR}/include -I ${CMAKE_SOURCE_DIR} ${X}")
#        endforeach ()
#
        # FILE(GLOB PROTO_SRCS ${CMAKE_BINARY_DIR}/include/protobuf/*.pb.*)

#        protobuf_generate(
#                ${PROJECT_NAME}
#                LANGUAGE grpc
#                OUT_VAR PROTO_SRCS
#                PROTOS ${PROTOS}
#                GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
#                PROTOC_OUT_DIR ${CMAKE_BINARY_DIR}/include
#                PLUGIN protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
#        )

        message(STATUS "${Protobuf_PROTOC_EXECUTABLE} ${PROTO_SRCS} ${PROTO_HDRS}")

#        foreach (X IN LISTS PROTOS)
#            execute_process(COMMAND bash -c
#                    "${Protobuf_PROTOC_EXECUTABLE} --grpc_out :${CMAKE_BINARY_DIR}/include/grpc -I ${CMAKE_SOURCE_DIR} ${X} --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>")
#        endforeach ()
#
#        FILE(GLOB PROTO_SRCS ${CMAKE_BINARY_DIR}/include/grpc/protobuf/*.pb.*)

#        protobuf_generate(${PROJECT_NAME} LANGUAGE cpp OUT_VAR PROTO_SRCS PROTOS protobuf/helloworld.proto
#            PROTOC_OUT_DIR ${CMAKE_BINARY_DIR}/include
#            PROTOC_EXE ${Protobuf_PROTOC_EXECUTABLE})
    endif ()


    add_compile_definitions(MANAPIHTTP_HTTP_AS_EXECUTABLE)
    if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
        add_executable(${PROJECT_NAME} main.cpp handlers.hpp handlers.cpp ${MANAPIHTTP_HTTP_FILES} ${PROTO_SRCS} ${PROTO_HDRS})
    else ()
        add_executable(${PROJECT_NAME}
                test_package/test_http.hpp
                test_package/test_json.hpp
                test_package/test_parse.hpp
                test_package/test_package.cpp
                test_package/test_bigint.hpp
                test_package/test_json_masks.hpp
                test_package/utest.h
                ${MANAPIHTTP_HTTP_FILES} ${PROTO_SRCS} ${PROTO_HDRS})
    endif ()
else ()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")

    # LIB
    message(STATUS "Build type 'lib'")

    add_compile_definitions(MANAPIHTTP_HTTP_AS_LIBRARY)
    if (BUILD_SHARED_LIBS)
        add_library(${PROJECT_NAME} SHARED ${MANAPIHTTP_HTTP_FILES})
    else ()
        add_library(${PROJECT_NAME} ${MANAPIHTTP_HTTP_FILES})
    endif ()

    set_target_properties(${PROJECT_NAME} PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR})

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PROJECT_NAME}.pc.in ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc @ONLY)
endif ()

set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${PROJECT_VERSION})

set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if (MANAPIHTTP_BUILD_METHOD STREQUAL "conan")
    target_link_libraries (${PROJECT_NAME} PUBLIC uv)

    if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
        target_link_libraries(${PROJECT_NAME} PUBLIC PostgreSQL::PostgreSQL)
    endif ()

    if (MANAPIHTTP_CPPTRACE_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC cpptrace::cpptrace)
    endif ()

    if (MANAPIHTTP_GMP_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC gmp::gmp)
    endif ()

    if (MANAPIHTTP_ZLIB_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ZLIB::ZLIB)
    endif ()

    if (MANAPIHTTP_CURL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC CURL::libcurl)
    endif ()

    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC openssl::openssl)
    endif ()

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC wolfssl::wolfssl)
    endif ()

    if (MANAPIHTTP_QUICHE_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC quiche::quiche)
    endif ()

    if (MANAPIHTTP_TQUIC_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC tquic::tquic)
    endif ()

    if (MANAPIHTTP_BROTLI_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC brotli::brotli)
    endif ()

    if (MANAPIHTTP_GRPC_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC protobuf::libprotobuf gRPC::grpc gRPC::grpc++_reflection)
    endif ()

    if (MANAPIHTTP_ZSTD_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${zstd_COMPONENT_NAMES})
    endif ()

    if (MANAPIHTTP_NGHTTP2_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC libnghttp2::libnghttp2)
    endif ()

    if (MANAPIHTTP_NGHTTP3_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC nghttp3::nghttp3)
    endif ()

    target_link_libraries(${PROJECT_NAME} PUBLIC
            ${CONAN_LIBS}
    )
else ()
    message(STATUS "libuv: libs=${LIBUV_LIBRARIES} include=${LIBUV_INCLUDE_DIRS}")
    target_link_libraries       (${PROJECT_NAME} PUBLIC ${LIBUV_LIBRARIES})
    target_include_directories  (${PROJECT_NAME} PUBLIC ${LIBUV_INCLUDE_DIRS})

    if (MANAPIHTTP_GMP_DEPENDENCY)
        message(STATUS "gmp: libs=${GMP_LIBRARIES} include=${GMP_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${GMP_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${GMP_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_ZLIB_DEPENDENCY)
        message(STATUS "zlib: libs=${ZLIB_LIBRARIES} include=${ZLIB_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${ZLIB_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${ZLIB_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_CURL_DEPENDENCY)
        message(STATUS "curl: libs=${CURL_LIBRARIES} include=${CURL_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${CURL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${CURL_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        message(STATUS "openssl: libs=${OPENSSL_LIBRARIES} include=${OPENSSL_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${OPENSSL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${OPENSSL_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        message(STATUS "wolfssl: libs=${WOLFSSL_LIBRARIES} include=${WOLFSSL_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${WOLFSSL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${WOLFSSL_INCLUDE_DIRS})
    endif ()


    if (MANAPIHTTP_QUICHE_DEPENDENCY)
        message(STATUS "quiche: libs=${QUICHE_LIBRARIES} include=${QUICHE_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${QUICHE_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${QUICHE_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_MSQUIC_DEPENDENCY)
        message(STATUS "msquic: libs=${MSQUIC_LIBRARIES} include=${MSQUIC_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${MSQUIC_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${MSQUIC_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_TQUIC_DEPENDENCY)
        message(STATUS "tquic: libs=${TQUIC_LIBRARIES} include=${TQUIC_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${TQUIC_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${TQUIC_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_BROTLI_DEPENDENCY)
        message(STATUS "brotli: libs=${BROTLI_LIBRARIES} include=${BROTLI_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${BROTLI_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${BROTLI_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_ZSTD_DEPENDENCY)
        message(STATUS "zstd: libs=${ZSTD_LIBRARIES} include=${ZSTD_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${ZSTD_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${ZSTD_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_GRPC_DEPENDENCY)
        message(STATUS "grpc: libs=${GRPC_LIBRARIES} include=${GRPC_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${GRPC_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${GRPC_INCLUDE_DIRS})

        message(STATUS "protobuf: libs=${PROTOBUF_LIBRARIES} include=${PROTOBUF_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${PROTOBUF_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${PROTOBUF_INCLUDE_DIRS})

        message(STATUS "grpcpp: libs=${GRPCPP_LIBRARIES} include=${GRPCPP_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${GRPCPP_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${GRPCPP_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_NGHTTP2_DEPENDENCY)
        message(STATUS "nghttp2: libs=${LIBNGHTTP2_LIBRARIES} include=${LIBNGHTTP2_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBNGHTTP2_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${LIBNGHTTP2_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_NGHTTP3_DEPENDENCY)
        message(STATUS "nghttp3: libs=${LIBNGHTTP3_LIBRARIES} include=${LIBNGHTTP3_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBNGHTTP3_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${LIBNGHTTP3_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_CPPTRACE_DEPENDENCY)
        message(STATUS "cpptrace: libs=${CPPTRACE_LIBRARIES} include=${CPPTRACE_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${CPPTRACE_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${CPPTRACE_INCLUDE_DIRS})
    endif ()


endif ()

if (MANAPIHTTP_GMP_DEPENDENCY_FETCH)
    add_dependencies(${PROJECT_NAME} gmp)
endif ()

if (MANAPIHTTP_ZLIB_DEPENDENCY_FETCH)
    add_dependencies(${PROJECT_NAME} zlib)
endif ()

if (MANAPIHTTP_LIBUV_DEPENDENCY_FETCH)
    add_dependencies(${PROJECT_NAME} libuv)
endif ()

if (MANAPIHTTP_CURL_DEPENDENCY_FETCH)
    add_dependencies(${PROJECT_NAME} curl)
endif ()

if (MANAPIHTTP_OPENSSL_DEPENDENCY_FETCH)
    add_dependencies(${PROJECT_NAME} openssl)
endif ()

if (MANAPIHTTP_NGHTTP2_DEPENDENCY_FETCH)
    add_dependencies(${PROJECT_NAME} libnghttp2)
endif ()

if (MANAPIHTTP_NGHTTP3_DEPENDENCY_FETCH)
    add_dependencies(${PROJECT_NAME} libnghttp3)
endif ()

if(BUILD_SHARED_LIBS)
    message(STATUS "shared build")
else ()
    message(STATUS "static build")
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif ()

if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
    if (WIN32)

    else ()
        target_link_libraries(${PROJECT_NAME} PRIVATE stdc++exp)
    endif()
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE stdc++_libbacktrace)
endif()

# Include public headers from the folder
target_include_directories  (${PROJECT_NAME} PRIVATE include)
target_include_directories  (${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/include)
target_include_directories  (${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/generated)

if (WIN32)
    # set(CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} "/FS")
endif ()

if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
    # nothing
else ()
    set(pc_libs_private)
    set(pc_req_private)
    set(pc_req_public)

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
            RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
    install(DIRECTORY ${CMAKE_BINARY_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})

    install(EXPORT ManapiHttpConfig DESTINATION lib/cmake/${PROJECT_NAME})

    set_target_properties(${PROJECT_NAME} PROPERTIES
            COMPILE_PDB_NAME ${PROJECT_NAME}
            COMPILE_PDB_OUTPUT_DIRECTORY ${CMAKE_INSTALL_BINDIR}
    )

    export(TARGETS ${PROJECT_NAME} FILE ManapiHttpConfig.cmake)
endif ()