# cmake_minimum_required(VERSION 3.28.2) # CXX Modules Support
cmake_minimum_required(VERSION 3.10)
project(manapihttp LANGUAGES CXX VERSION 1.0.5 DESCRIPTION "Fast HTTP Server/Client Library for easy develop API-Services and Web Applications")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
#set(CMAKE_CXX_VISIBILITY_PRESET hidden)
#set(CMAKE_VISIBILITY_INLINES_HIDDEN TRUE)

cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0070 NEW)

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif ()

include(CTest)

if (WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if (NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
else ()
    message(STATUS "This project is a top-level one")
endif ()

OPTION(BUILD_SHARED_LIBS "Build shared libs" OFF)
OPTION(MANAPIHTTP_GMP_DEPENDENCY "BigInt support by GMP" ON)
OPTION(MANAPIHTTP_CURL_DEPENDENCY "Fetch support by CURL" ON)
OPTION(MANAPIHTTP_ZLIB_DEPENDENCY "Compress support by zlib" ON)
OPTION(MANAPIHTTP_JSON_DEBUG "JSON debug variables" ON)
OPTION(MANAPIHTTP_QUICHE_DEPENDENCY "The QUIC Implementation by Cloudflare" ON)
OPTION(MANAPIHTTP_TQUIC_DEPENDENCY "The QUIC Implementation by Tencent" ON)
OPTION(MANAPIHTTP_MSQUIC_DEPENDENCY "The QUIC Implementation by Microsoft" ON)
OPTION(MANAPIHTTP_OPENSSL_DEPENDENCY "TLS, QUIC and etc implementations by OpenSSL" ON)
OPTION(MANAPIHTTP_WOLFSSL_DEPENDENCY "TLS, QUIC and etc implementations by WolfSSL" ON)
OPTION(MANAPIHTTP_GRPC_DEPENDENCY "gPRC implementations by Google" ON)
OPTION(MANAPIHTTP_BROTLI_DEPENDENCY "Brotli Enabled" ON)
OPTION(MANAPIHTTP_ZSTD_DEPENDENCY "ZSTD Enabled" ON)
OPTION(MANAPIHTTP_BUILD_METHOD "Build method" "default")
OPTION(MANAPIHTTP_WOLFSSL_WITH_ALPN "WolfSSL with ALPN" ON)
OPTION(MANAPIHTTP_NGHTTP2_DEPENDENCY "libnghttp2 by nghttp2" ON)
OPTION(MANAPIHTTP_NGHTTP3_DEPENDENCY "libnghttp3 by nghttp3" ON)
OPTION(MANAPIHTTP_CPPTRACE_DEPENDENCY "Stack Tracing c++" OFF)
OPTION(MANAPIHTTP_DEPENDENCY_FETCH_SHARED "Build Shared Dependency" ON)
OPTION(MANAPIHTTP_STD_BACKTRACE_DEPENDENCY "backtrace utils by std" ON)
OPTION(MANAPIHTTP_OPENSSL_SYSTEM_DISABLE "Disable default OpenSSL" OFF)
OPTION(MANAPIHTTP_CURL_SYSTEM_DISABLE "Disable default cURL" OFF)
OPTION(MANAPIHTTP_NGHTTP2_SYSTEM_DISABLE "Disable default nghttp2" OFF)
OPTION(MANAPIHTTP_NGHTTP3_SYSTEM_DISABLE "Disable default nghttp3" OFF)
OPTION(MANAPIHTTP_DISABLE_TRACE "Disable Trace Debug" OFF)
OPTION(MANAPIHTTP_DISABLE_TRACE_HARD "Disable Trace Hard Debug" OFF)
OPTION(MANAPIHTTP_NGHTTP3_SYSTEM_DISABLE "Disable default nghttp3" OFF)
OPTION(MANAPIHTTP_QT_DEPENDENCY "Qt dependency" OFF)
OPTION(MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT "Output Directory For Libraries" OFF)
OPTION(MANAPIHTTP_BUILD_TYPE "Build result: binary or library" "lib")
OPTION(MANAPIHTTP_INSTALL_ARCH "/x86_64-linux-gnu for example" "")
OPTION(MANAPIHTTP_ENABLE_TESTS "Enable Tests" OFF)

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
message(STATUS "libnghttp2 by nghttp2: ${MANAPIHTTP_NGHTTP2_DEPENDENCY}")
message(STATUS "libnghttp3 by nghttp3: ${MANAPIHTTP_NGHTTP3_DEPENDENCY}")
message(STATUS "qt dependency: ${MANAPIHTTP_QT_DEPENDENCY}")

set(MANAPIHTTP_GMP_DEPENDENCY_OBJ OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
        find_package(PostgreSQL)
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

    if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
        find_package(PostgreSQL)
    endif()

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
                set(MANAPIHTTP_LIBUV_LIBNAME "${CMAKE_SHARED_LIBRARY_PREFIX}uv${CMAKE_SHARED_LIBRARY_SUFFIX}")
            else ()
                set(MANAPIHTTP_LIBUV_LIBNAME "${CMAKE_STATIC_LIBRARY_PREFIX}uv${CMAKE_STATIC_LIBRARY_SUFFIX}")
            endif ()

            if (MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT)
                set(LIBUV_LIBRARIES "${MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT}/uv/${MANAPIHTTP_LIBUV_LIBNAME}")
            else ()
                set(LIBUV_LIBRARIES "${MANAPIHTTP_LIBUV_INSTALL_DIR}/lib/${MANAPIHTTP_LIBUV_LIBNAME}")
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

    if (MANAPIHTTP_QT_DEPENDENCY)
        find_package(Qt6 COMPONENTS Core Widgets Quick)
        if (Qt6_FOUND)
            set(CMAKE_AUTOMOC ON)
        else ()
            message(STATUS "qt6 not found")
            set(MANAPIHTTP_QT_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_GMP_DEPENDENCY)
        pkg_check_modules(GMP gmp)
        # set(GMP_FOUND OFF)

        if (NOT GMP_FOUND)
            if (MANAPIHTTP_DEPENDENCY_FETCH)
                set(MANAPIHTTP_GMP_INSTALL_DIR ${MANAPIHTTP_DEPS_PREFIX}/lib/gmp)
                if (MANAPIHTTP_DEPENDENCY_FETCH_SHARED)
                    set(MANAPIHTTP_GMP_LIBNAME "${CMAKE_SHARED_LIBRARY_PREFIX}gmp${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(MANAPIHTTP_GMP_LIBNAME "${CMAKE_STATIC_LIBRARY_PREFIX}gmp${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()

                if (MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT)
                    set(GMP_LIBRARIES "${MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT}/gmp/${MANAPIHTTP_GMP_LIBNAME}")
                else ()
                    set(GMP_LIBRARIES "${MANAPIHTTP_GMP_INSTALL_DIR}/lib/${MANAPIHTTP_GMP_LIBNAME}")
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
                set(MANAPIHTTP_GMP_DEPENDENCY_FETCH OFF)
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
                    set(MANAPIHTTP_ZLIB_LIBNAME "${CMAKE_SHARED_LIBRARY_PREFIX}z${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(MANAPIHTTP_ZLIB_LIBNAME "${CMAKE_STATIC_LIBRARY_PREFIX}z${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()

                if (MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT)
                    set(ZLIB_LIBRARIES "${MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT}/z/${MANAPIHTTP_ZLIB_LIBNAME}")
                else ()
                    set(ZLIB_LIBRARIES "${MANAPIHTTP_ZLIB_INSTALL_DIR}/lib/${MANAPIHTTP_ZLIB_LIBNAME}")
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
                set(MANAPIHTTP_ZLIB_DEPENDENCY_FETCH OFF)
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
                    set(MANAPIHTTP_OPENSSL_LIBNAME1 "${CMAKE_SHARED_LIBRARY_PREFIX}crypto${CMAKE_SHARED_LIBRARY_SUFFIX}")
                    set(MANAPIHTTP_OPENSSL_LIBNAME2 "${CMAKE_SHARED_LIBRARY_PREFIX}ssl${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(MANAPIHTTP_OPENSSL_LIBNAME1 "${CMAKE_STATIC_LIBRARY_PREFIX}crypto${CMAKE_STATIC_LIBRARY_SUFFIX}")
                    set(MANAPIHTTP_OPENSSL_LIBNAME2 "${CMAKE_STATIC_LIBRARY_PREFIX}ssl${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()

                if (MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT)
                    set(OPENSSL_LIBRARIES "${MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT}/openssl/${MANAPIHTTP_OPENSSL_LIBNAME1};${MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT}/openssl/${MANAPIHTTP_OPENSSL_LIBNAME2}")
                else ()
                    set(OPENSSL_LIBRARIES "${MANAPIHTTP_OPENSSL_INSTALL_DIR}/lib64/${MANAPIHTTP_OPENSSL_LIBNAME1};${MANAPIHTTP_OPENSSL_INSTALL_DIR}/lib64/${MANAPIHTTP_OPENSSL_LIBNAME2}")
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
                set(MANAPIHTTP_OPENSSL_DEPENDENCY_FETCH OFF)
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
                    set(MANAPIHTTP_CURL_LIBNAME "${CMAKE_SHARED_LIBRARY_PREFIX}curl${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(MANAPIHTTP_CURL_LIBNAME "${CMAKE_STATIC_LIBRARY_PREFIX}curl${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()

                if (MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT)
                    set(CURL_LIBRARIES "${MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT}/curl/${MANAPIHTTP_CURL_LIBNAME}")
                else ()
                    set(CURL_LIBRARIES "${MANAPIHTTP_CURL_INSTALL_DIR}/lib/${MANAPIHTTP_CURL_LIBNAME}")
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
                set(MANAPIHTTP_CURL_DEPENDENCY_FETCH OFF)
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

        find_library(GRPCPP_REFLECTION NAMES grpc++_reflection)
        if (NOT GRPCPP_REFLECTION)
            set(GRPCPP_REFLECTION "")
        endif ()
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
                if (MANAPIHTTP_DEPENDENCY_FETCH_SHARED)
                    set(MANAPIHTTP_LIBNGHTTP2_LIBNAME "${CMAKE_SHARED_LIBRARY_PREFIX}nghttp2${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(MANAPIHTTP_LIBNGHTTP2_LIBNAME "${CMAKE_STATIC_LIBRARY_PREFIX}nghttp2${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()

                if (MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT)
                    set(LIBNGHTTP2_LIBRARIES "${MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT}/nghttp2/${MANAPIHTTP_LIBNGHTTP2_LIBNAME}")
                else ()
                    set(LIBNGHTTP2_LIBRARIES "${MANAPIHTTP_LIBNGHTTP2_INSTALL_DIR}/lib/${MANAPIHTTP_LIBNGHTTP2_LIBNAME}")
                endif ()

                set(MANAPIHTTP_LIBNGHTTP2_INSTALL_DIR ${MANAPIHTTP_DEPS_PREFIX}/lib/libnghttp2)
                set(LIBNGHTTP2_INCLUDE_DIRS $<BUILD_INTERFACE:${MANAPIHTTP_LIBNGHTTP2_INSTALL_DIR}/include>)

                ExternalProject_Add(
                        libnghttp2
                        PREFIX "${MANAPIHTTP_DEPS_PREFIX}"
                        GIT_REPOSITORY https://github.com/nghttp2/nghttp2
                        GIT_TAG v1.66.0
                        CMAKE_ARGS
                        -DCMAKE_INSTALL_PREFIX:PATH=${MANAPIHTTP_LIBNGHTTP2_INSTALL_DIR}
                        -DBUILD_SHARED_LIBS=${MANAPIHTTP_DEPENDENCY_FETCH_SHARED}
                        BUILD_BYPRODUCTS "${LIBNGHTTP2_LIBRARIES}"
                )
                set(MANAPIHTTP_NGHTTP2_DEPENDENCY_FETCH ON)
            else ()
                set(MANAPIHTTP_NGHTTP2_DEPENDENCY_FETCH OFF)
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
                if (MANAPIHTTP_DEPENDENCY_FETCH_SHARED)
                    set(MANAPIHTTP_LIBNGHTTP3_LIBNAME "${CMAKE_SHARED_LIBRARY_PREFIX}nghttp3${CMAKE_SHARED_LIBRARY_SUFFIX}")
                else ()
                    set(MANAPIHTTP_LIBNGHTTP3_LIBNAME "${CMAKE_STATIC_LIBRARY_PREFIX}nghttp3${CMAKE_STATIC_LIBRARY_SUFFIX}")
                endif ()

                if (MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT)
                    set(LIBNGHTTP3_LIBRARIES "${MANAPIHTTP_DEPENDENCY_FETCH_OUTPUT}/nghttp3/${MANAPIHTTP_LIBNGHTTP3_LIBNAME}")
                else ()
                    set(LIBNGHTTP3_LIBRARIES "${MANAPIHTTP_LIBNGHTTP3_INSTALL_DIR}/lib/${MANAPIHTTP_LIBNGHTTP3_LIBNAME}")
                endif ()

                set(MANAPIHTTP_LIBNGHTTP3_INSTALL_DIR ${MANAPIHTTP_DEPS_PREFIX}/lib/libnghttp3)

                set(LIBNGHTTP3_INCLUDE_DIRS $<BUILD_INTERFACE:${MANAPIHTTP_LIBNGHTTP3_INSTALL_DIR}/include>)

                ExternalProject_Add(
                        libnghttp3
                        PREFIX "${MANAPIHTTP_DEPS_PREFIX}"
                        GIT_REPOSITORY https://github.com/ngtcp2/nghttp3
                        GIT_TAG v1.11.0
                        CMAKE_ARGS
                        -DCMAKE_INSTALL_PREFIX:PATH=${MANAPIHTTP_LIBNGHTTP3_INSTALL_DIR}
                        -DBUILD_SHARED_LIBS=${MANAPIHTTP_DEPENDENCY_FETCH_SHARED}
                        BUILD_BYPRODUCTS "${LIBNGHTTP3_LIBRARIES}"
                )
                set(MANAPIHTTP_NGHTTP3_DEPENDENCY_FETCH ON)
            else ()
                set(MANAPIHTTP_NGHTTP3_DEPENDENCY_FETCH OFF)
                set(MANAPIHTTP_NGHTTP3_DEPENDENCY OFF)
            endif ()
        endif ()
    endif ()
endif ()

include(CheckLibraryExists)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/preprocess/ManapiParams.hpp" TEXT)
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
string(REPLACE "{{ VAR_MANAPIHTTP_STD_BACKTRACE_DEPENDENCY }}" $<BOOL:${MANAPIHTTP_STD_BACKTRACE_DEPENDENCY}> TEXT "${TEXT}")
string(REPLACE "{{ VAR_MANAPIHTTP_IS_SHARED }}" $<BOOL:${BUILD_SHARED_LIBS}> TEXT "${TEXT}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/ManapiParams.hpp" CONTENT "${TEXT}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/include/ManapiParams.hpp")



set(MANAPIHTTP_HTTP_FILES
        include/ManapiHttp.hpp
        include/ManapiThreadPool.hpp
        include/ManapiUtils.hpp
        include/http/ManapiHttpResponse.hpp
        include/http/ManapiHttpRequest.hpp
        include/ManapiBigint.hpp
        include/http/ManapiHttpTypes.hpp
        include/fs/ManapiFilesystem.hpp
        include/crypto/ManapiBase64.hpp
        include/compress/ManapiCompress.hpp
        include/hash/ManapiSHA256.hpp
        include/ManapiFetch.hpp
        include/ManapiTimerPool.hpp
        include/http/ManapiHttpPool.hpp
        include/json/ManapiJsonMask.hpp
        include/http/ManapiSite.hpp
        include/http/ManapiHttpConfig.hpp
        include/ext/ManapiJsonWebTokenTraits.hpp
        include/ext/ManapiJsonWebTokenDefaults.hpp
        include/encoding/ManapiUnicode.hpp
        include/json/ManapiJsonBuilder.hpp
        include/std/ManapiBeforeDelete.hpp
        include/worker/ManapiBaseWorker.hpp
        include/http/ManapiBaseHttp.hpp
        include/worker/ManapiOpenSslOverTcp.hpp
        src/include/http/ManapiHttp1.hpp
        src/include/http/ManapiHttp2.hpp
        include/compress/ManapiHPack.hpp
        src/include/http/ManapiHttp2Interface.hpp
        src/include/http/ManapiHttp1Interface.hpp
        include/http/ManapiHttpMime.hpp
        include/worker/ManapiTcp.hpp
        include/json/ManapiJson.hpp
        ${CMAKE_CURRENT_BINARY_DIR}/include/ManapiParams.hpp

        src/ManapiHttp.cpp
        src/ManapiThreadPool.cpp
        src/ManapiUtils.cpp
        src/http/ManapiHttpResponse.cpp
        src/http/ManapiHttpRequest.cpp
        src/json/ManapiJson.cpp
        src/ManapiBigint.cpp
        src/compress/ManapiCompress.cpp
        src/fs/ManapiFilesystem.cpp
        src/hash/ManapiSHA256.cpp
        src/ManapiFetch.cpp
        src/json/ManapiJsonMask.cpp
        src/http/ManapiHttpPool.cpp
        include/cache/ManapiLRU.hpp
        src/ManapiTimerPool.cpp
        src/http/ManapiSite.cpp
        src/http/ManapiHttpConfig.cpp
        src/ManapiUnicode.cpp
        src/json/ManapiJsonBuilder.cpp
        src/std/ManapiBeforeDelete.cpp
        src/worker/ManapiBaseWorker.cpp
        src/http/ManapiBaseHttp.cpp
        src/worker/ManapiOpenSslOverTcp.cpp
        src/http/ManapiHttp1.cpp
        src/http/ManapiHttp2.cpp
        src/http/ManapiHttp2Interface.cpp
        src/http/ManapiHttpMime.cpp
        src/worker/ManapiTcp.cpp
        src/http/ManapiFormData.cpp
        include/http/ManapiFormData.hpp
        src/ManapiAsync.cpp
        include/ManapiAsync.hpp
        include/std/ManapiAsyncMutex.hpp
        include/std/ManapiAsyncConditionVariable.hpp
        include/std/ManapiChain.hpp
        include/std/ManapiAsyncTimer.hpp
        include/crypto/ManapiAEAD.hpp
        include/crypto/ManapiHKDF.hpp
        include/crypto/ManapiCryptoUtils.hpp
        include/crypto/ManapiAES.hpp
        src/worker/ManapiUdp.cpp
        include/worker/ManapiUdp.hpp
        include/ManapiInitTools.hpp
        include/worker/ManapiHttp3Cloudflare.hpp
        src/worker/ManapiHttp3Cloudflare.cpp
        include/std/ManapiAsyncPromise.hpp
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
        include/worker/ManapiWolfSslOverTcp.hpp
        src/worker/ManapiWolfSslOverTcp.cpp
        include/ManapiEventLoop.hpp
        src/ManapiEventLoop.cpp
        include/std/ManapiAsyncContext.hpp
        src/std/ManapiAsyncContext.cpp
        src/std/ManapiAsyncMutex.cpp
        src/std/ManapiAsyncConditionVariable.cpp
        include/fs/ManapiFileStream.hpp
        src/fs/ManapiFileStream.cpp
        include/ext/pq/AsyncPostgreClient.hpp
        include/std/ManapiAsyncSocket.hpp
        include/ext/pq/AsyncPostgreResult.hpp
        include/ext/pq/AsyncPostgreRow.hpp
        include/ext/pq/AsyncPostgreField.hpp
        include/ext/pq/AsyncPostgreValue.hpp
        include/ext/pq/AsyncPostgreValueTypes.hpp
        include/ext/pq/AsyncPostgreNotification.hpp
        include/ext/pq/AsyncPostgrePool.hpp
        include/ext/pq/AsyncPostgreImpl.ipp
        include/std/ManapiAsyncParallelRun.hpp
        include/ManapiFetch2.hpp
        include/ManapiTimerObject.hpp
        src/ManapiTimerObject.cpp
        include/std/ManapiBuffer.hpp
        src/std/ManapiBuffer.cpp
        include/ManapiMemoryPool.hpp
        src/ManapiInitTools.cpp
        include/http/ManapiFileTransferInfo.hpp
        src/std/ManapiAsyncSocket.cpp
        include/std/ManapiCancellation.hpp
        src/std/ManapiCancellation.cpp
        include/http/ManapiURLDecodeStream.hpp
        src/http/ManapiURLDecodeStream.cpp
        src/http/ManapiURLParams.cpp
        include/http/ManapiURLParams.hpp
        src/http/ManapiHttpTypes.cpp
        src/encoding/ManapiURL.cpp
        include/ManapiVersions.hpp
        src/worker/ManapiTlsOverTcp.cpp
        include/worker/ManapiTlsOverTcp.hpp
        src/std/ManapiAsyncLogger.cpp
        include/std/ManapiAsyncLogger.hpp
        include/ManapiEventStructures.hpp
        src/ManapiEventStructures.cpp
        src/include/ManapiWindows.hpp
        src/crypto/ManapiCryptoUtils.cpp
        src/include/ManapiHttpStructs.hpp
        src/include/ManapiDefaultErrors.hpp
        include/ManapiProcess.hpp
        src/ManapiProcess.cpp
        include/http/ManapiSiteCtx.hpp
        src/http/ManapiSiteCtx.cpp
        include/std/ManapiAsyncThreadsMutex.hpp
        src/std/ManapiAsyncThreadsMutex.cpp
        include/std/ManapiEasyCancellation.hpp
        src/std/ManapiEasyCancellation.cpp
        src/ManapiMemoryPool.cpp
        include/worker/ManapiHttp2Worker.hpp
        src/worker/ManapiHttp2Worker.cpp
        src/http/ManapiHttp1Interface.cpp
        include/worker/ManapiInterfaceWorker.hpp
        src/worker/ManapiInterfaceWorker.cpp
        include/std/ManapiSlice.hpp
        src/std/ManapiSlice.cpp
        src/compress/ManapiHPack.cpp
        src/include/http/ManapiNgHttp2Interface.hpp
        src/http/ManapiNgHttp2Interface.cpp
        src/include/ManapiJsonMaskUtils.hpp
        src/include/ManapiDebug.hpp
        include/ManapiUtils.hpp
        src/crypto/ManapiAEAD.cpp
        src/crypto/ManapiHKDF.cpp
        src/crypto/ManapiAES.cpp
        include/utils/ManapiMultithreadStorage.hpp
        src/utils/ManapiMultithreadStorage.cpp
        include/ManapiGrpc.hpp
        src/ManapiGrpc.cpp
        include/utils/ManapiConfig.hpp
        src/utils/ManapiConfig.cpp
        src/include/ManapiInternalGrpc.hpp
        include/ManapiDns.hpp
        src/ManapiDns.cpp
        src/ManapiTime.cpp
        src/crypto/ManapiBase64.cpp
        src/ManapiFetch2.cpp
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
        include/std/ManapiFunction.hpp
        include/ext/pq/AsyncPostgreError.hpp
        src/include/ManapiEventLoopCurl.hpp
        include/std/ManapiRef.hpp
        include/std/ManapiScopePtr.hpp
        include/cache/ManapiTL.hpp
)

set(MANAPIHTTP_MODULES
        modules/ManapiJson.cppm
        modules/ManapiJsonBuilder.cppm
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


            if (MANAPIHTTP_BUILD_METHOD STREQUAL "conan")
                set(GRPC_CPP_PLUGIN $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
            else ()
                find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
            endif ()

            message(STATUS "GRPC_CPP_PLUGIN=${GRPC_CPP_PLUGIN}")

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


    if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
        add_executable(${PROJECT_NAME})
    endif ()

    target_compile_definitions(${PROJECT_NAME} PRIVATE MANAPIHTTP_HTTP_AS_EXECUTABLE)
    target_sources(${PROJECT_NAME} PRIVATE main.cpp impl.cpp handlers.hpp handlers.cpp ${MANAPIHTTP_HTTP_FILES} ${PROTO_SRCS} ${PROTO_HDRS})

    #    target_sources(${PROJECT_NAME} PUBLIC
    #            FILE_SET CXX_MODULES
    #            BASE_DIRS ${CMAKE_SOURCE_DIR}/modules
    #            FILES ${MANAPIHTTP_MODULES}
    #    )

else ()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")

    # LIB
    message(STATUS "Build type 'lib'")

    if (BUILD_SHARED_LIBS)
        add_library(${PROJECT_NAME} SHARED)
    else ()
        add_library(${PROJECT_NAME})
    endif ()

    target_sources(${PROJECT_NAME} PRIVATE ${MANAPIHTTP_HTTP_FILES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE MANAPIHTTP_HTTP_AS_LIBRARY)
    set_target_properties(${PROJECT_NAME} PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR})

    #    target_sources(${PROJECT_NAME} PUBLIC
    #            FILE_SET CXX_MODULES
    #            BASE_DIRS ${CMAKE_SOURCE_DIR}/modules
    #            FILES ${MANAPIHTTP_MODULES}
    #    )

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PROJECT_NAME}.pc.in ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc @ONLY)
endif ()

target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fmerge-all-constants>)
# target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fmodules>)
# target_compile_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fmodules-ts>)
set_target_properties(${PROJECT_NAME} PROPERTIES UNITY_BUILD ON)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

include(GenerateExportHeader)

if (MANAPIHTTP_BUILD_TYPE STREQUAL "lib")
    generate_export_header(${PROJECT_NAME} EXPORT_FILE_NAME ./include/ManapiExport.hpp)
endif ()

set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${PROJECT_VERSION})

if (DEFINED quiche_VERSION)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MANAPIHTTP_QUICHE_VERSION="${quiche_VERSION}")
elseif (DEFINED QUICHE_VERSION)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MANAPIHTTP_QUICHE_VERSION="${QUICHE_VERSION}")
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Build type: Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE MANAPIHTTP_HTTP_BUILD_DEBUG)
endif ()


if (MANAPIHTTP_DISABLE_TRACE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MANAPIHTTP_DISABLE_TRACE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MANAPIHTTP_DISABLE_TRACE_HARD)
elseif (MANAPIHTTP_DISABLE_TRACE_HARD)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MANAPIHTTP_DISABLE_TRACE_HARD)
endif ()

if (WIN32 AND BUILD_SHARED_LIBS)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MANAPIHTTP_BUILD_SHARED_LIBS)
endif()

set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if (MANAPIHTTP_BUILD_METHOD STREQUAL "conan")
    get_target_property(LIBUV_INCLUDE_DIRECTORIES uv INTERFACE_INCLUDE_DIRECTORIES)

    message(STATUS ${libuv_CONAN_INCLUDE_DIRS})
    if (NOT EXISTS "${CMAKE_BINARY_DIR}/include/ext/uv.h")
        file(COPY ${libuv_CONAN_INCLUDE_DIRS}/uv DESTINATION ${CMAKE_BINARY_DIR}/include/ext FILES_MATCHING PATTERN "*.h")
        file(COPY ${libuv_CONAN_INCLUDE_DIRS}/uv.h DESTINATION ${CMAKE_BINARY_DIR}/include/ext FILES_MATCHING PATTERN "*.h")
        if (NOT EXISTS "${CMAKE_BINARY_DIR}/include/ext/uv.h")
            message( FATAL_ERROR "Failed to copy uv.h from ${libuv_CONAN_INCLUDE_DIRS}" )
        endif()
    endif()


    target_link_libraries (${PROJECT_NAME} PUBLIC uv)

    if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
        target_link_libraries(${PROJECT_NAME} PUBLIC libpq::libpq)
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
    endqif ()

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

    if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
        message(STATUS "libpq: libs=${PostgreSQL_LIBRARIES} include=${PostgreSQL_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${PostgreSQL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${PostgreSQL_INCLUDE_DIRS})
    endif()

    if (MANAPIHTTP_QT_DEPENDENCY)
        message(STATUS "qt: libs=Qt6::Core Qt6::Widgets Qt6::Quick")
        target_link_libraries(${PROJECT_NAME} PUBLIC Qt6::Core Qt6::Widgets Qt6::Quick)
    endif ()

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

        message(STATUS "grpcpp: libs=${GRPCPP_LIBRARIES}${GRPCPP_REFLECTION} include=${GRPCPP_INCLUDE_DIRS}")
        target_link_libraries(${PROJECT_NAME} PUBLIC ${GRPCPP_LIBRARIES} ${GRPCPP_REFLECTION})
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

    if (NOT EXISTS "${CMAKE_BINARY_DIR}/include/uv.h")
        if (MANAPIHTTP_LIBUV_INSTALL_DIR)
            set(copy_MANAPIHTTP_LIBUV_INSTALL_DIR ${MANAPIHTTP_LIBUV_INSTALL_DIR})
        else()
            if (LIBUV_INCLUDE_DIRS)
                set(copy_MANAPIHTTP_LIBUV_INSTALL_DIR ${LIBUV_INCLUDE_DIRS})
            else()
                if (EXISTS "/usr/include/uv.h")
                    set(copy_MANAPIHTTP_LIBUV_INSTALL_DIR "/usr/include")
                elseif (EXISTS "/usr/include/x86_64-linux-gnu/uv.h")
                    set(copy_MANAPIHTTP_LIBUV_INSTALL_DIR "/usr/include/x86_64-linux-gnu")
                elseif (MANAPIHTTP_INSTALL_ARCH)
                    set(copy_MANAPIHTTP_LIBUV_INSTALL_DIR "/usr/include${MANAPIHTTP_INSTALL_ARCH}")
                endif()
            endif()
        endif()

        file(COPY ${copy_MANAPIHTTP_LIBUV_INSTALL_DIR}/uv DESTINATION ${CMAKE_BINARY_DIR}/include/ext FILES_MATCHING PATTERN "*.h")
        file(COPY ${copy_MANAPIHTTP_LIBUV_INSTALL_DIR}/uv.h DESTINATION ${CMAKE_BINARY_DIR}/include/ext FILES_MATCHING PATTERN "*.h")

        if (NOT EXISTS "${CMAKE_BINARY_DIR}/include/ext/uv.h")
            message( FATAL_ERROR "Failed to copy uv.h from ${libuv_CONAN_INCLUDE_DIRS}" )
        endif()
    endif()

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

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_precompile_headers(${PROJECT_NAME} PRIVATE
            <vector>
            <string>
            <map>
            <unordered_map>
            <memory>
            <algorithm>
            <filesystem>
            <cstring>
            <functional>
            <deque>
            <limits>
    )
endif()

if(BUILD_SHARED_LIBS)
    message(STATUS "shared build")
else ()
    message(STATUS "static build")
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif ()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE -pipe -fno-semantic-interposition)
    if (UNIX)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
    endif()
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -fno-stack-check -fno-address-sanitizer-use-after-scope)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
    if (WIN32)

    else ()
        target_link_libraries(${PROJECT_NAME} PRIVATE stdc++exp)
    endif()
else()
    if (MANAPIHTTP_STD_BACKTRACE_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PRIVATE stdc++_libbacktrace)
    endif ()
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /MP)
endif()
# Include public headers from the folder
target_include_directories  (${PROJECT_NAME} PRIVATE include)
target_include_directories  (${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/include)
target_include_directories  (${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/generated)

if (WIN32)
    # set(CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} "/FS")
endif ()

if (MANAPIHTTP_ENABLE_TESTS AND MANAPIHTTP_BUILD_TYPE STREQUAL "lib")
    message(STATUS "Enable Testing")

    add_subdirectory(./tests)

    enable_testing()
else ()
    message(STATUS "Disable Testing")
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

    if (MANAPIHTTP_INSTALL_ARCH)
        set(CMAKE_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR}${MANAPIHTTP_INSTALL_ARCH})
        set(CMAKE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}${MANAPIHTTP_INSTALL_ARCH})
    endif ()


    install(TARGETS ${PROJECT_NAME} EXPORT manapihttp-targets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            #            FILE_SET CXX_MODULES
            #            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}/src
            #            CXX_MODULES_BMI
            #            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}/bmi-${CMAKE_CXX_COMPILER_ID}_$<CONFIG>
            #            FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECTNAME}
    )

    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
    install(DIRECTORY ${CMAKE_BINARY_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})

    install(EXPORT manapihttp-targets
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
            #            CXX_MODULES_DIRECTORY .
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
            COMPILE_PDB_NAME ${PROJECT_NAME}
            COMPILE_PDB_OUTPUT_DIRECTORY ${CMAKE_INSTALL_BINDIR}
    )

    install(FILES cmake/manapihttp-config.cmake
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
    )
    #
    #    export(TARGETS ${PROJECT_NAME} FILE ManapiHttpConfig.cmake)
endif ()

#include("${manapihttp_PACKAGE_FOLDER_DEBUG}/lib/cmake/manapihttp/manapihttp-config.cmake")
#
#set_target_properties(manapihttp::manapihttp PROPERTIES
#        CXX_EXTENSIONS "OFF"
#        CXX_MODULE_STD ""
#        IMPORTED_CXX_MODULES_COMPILE_DEFINITIONS "MANAPIHTTP_HTTP_BUILD_DEBUG;MANAPIHTTP_QUICHE_VERSION=\"0.24.4\";MANAPIHTTP_HTTP_AS_LIBRARY"
#        IMPORTED_CXX_MODULES_COMPILE_FEATURES "cxx_std_23"
#        IMPORTED_CXX_MODULES_INCLUDE_DIRECTORIES "/home/Timur/.conan2/p/b/manap001c9f726cfa4/b/include;/home/Timur/.conan2/p/b/manap001c9f726cfa4/b/build/Debug/include;/home/Timur/.conan2/p/b/manap001c9f726cfa4/b/build/Debug/generated;${manapihttp_PACKAGE_FOLDER_DEBUG}/include/manapihttp"
#        INTERFACE_COMPILE_FEATURES "cxx_std_23"
#        INTERFACE_INCLUDE_DIRECTORIES "${manapihttp_PACKAGE_FOLDER_DEBUG}/include/manapihttp"
#)
#
#target_sources(manapihttp::manapihttp
#        INTERFACE
#        FILE_SET "CXX_MODULES"
#        TYPE "CXX_MODULES"
#        BASE_DIRS "${manapihttp_PACKAGE_FOLDER_DEBUG}/lib/cmake/manapihttp/src"
#        FILES "${manapihttp_PACKAGE_FOLDER_DEBUG}/lib/cmake/manapihttp/src/ManapiJson.cppm"
#)
#
#set_property(TARGET "manapihttp::manapihttp"
#        PROPERTY IMPORTED_CXX_MODULES_DEBUG
#        "manapi=${manapihttp_PACKAGE_FOLDER_DEBUG}/lib/cmake/manapihttp/src/ManapiJson.cppm,${manapihttp_PACKAGE_FOLDER_DEBUG}/lib/cmake/manapihttp/bmi-GNU_Debug/manapi.gcm"
#)