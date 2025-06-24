cmake_minimum_required(VERSION 3.10)
project(manapihttp VERSION 0.0.2 DESCRIPTION "Fast HTTP Server/Client Library for easy develop API-Services and Web Applications")

cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0070 NEW)

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

OPTION(CMAKE_BUILD_TYPE "Library build type" "Release")
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

    if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
        # For Testing
        find_package(PostgreSQL REQUIRED)
        find_package(cpptrace REQUIRED)
    endif ()
else ()
    message(STATUS "Build method: default")

    find_package(PkgConfig REQUIRED)

    # include finders
    # include(./cmake/FindLibev.cmake)

    pkg_check_modules(LIBUV REQUIRED IMPORTED_TARGET libuv)

    if (MANAPIHTTP_GMP_DEPENDENCY)
        pkg_check_modules(GMP gmp)
        set(MANAPIHTTP_GMP_DEPENDENCY GMP_FOUND)
        if (NOT GMP_FOUND)
            set(MANAPIHTTP_GMP_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_ZLIB_DEPENDENCY)
        pkg_check_modules(ZLIB zlib)
        set(MANAPIHTTP_ZLIB_DEPENDENCY ZLIB_FOUND)
        if (NOT ZLIB_FOUND)
            set(MANAPIHTTP_ZLIB_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_CURL_DEPENDENCY)
        pkg_check_modules(CURL libcurl)
        set(MANAPIHTTP_CURL_DEPENDENCY CURL_FOUND)
        if (NOT CURL_FOUND)
            set(MANAPIHTTP_CURL_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        pkg_check_modules(WOLFSSL wolfssl)
        if (NOT WOLFSSL_FOUND)
            set(MANAPIHTTP_WOLFSSL_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        pkg_check_modules(OPENSSL openssl)
        if (NOT OPENSSL_FOUND)
            set(MANAPIHTTP_OPENSSL_DEPENDENCY OFF)
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
        if (NOT Brotli_FOUND)
            set(MANAPIHTTP_BROTLI_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_GRPC_DEPENDENCY)
        pkg_check_modules(GRPC grpc)
        if (NOT GRPC_FOUND)
            set(MANAPIHTTP_GRPC_DEPENDENCY OFF)
        endif ()
    endif ()

    if (MANAPIHTTP_ZSTD_DEPENDENCY)
        include(./cmake/FindZSTD.cmake)
        if (NOT ZSTD_FOUND)
            set(MANAPIHTTP_ZSTD_DEPENDENCY OFF)
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

file(GENERATE OUTPUT include/ManapiParams.hpp CONTENT "${TEXT}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS include/ManapiParams.hpp)

set(CMAKE_CXX_STANDARD 23)

set(MANAPIHTTP_HTTP_FILES
        include/ManapiHttp.hpp
        include/services/ManapiThreadPool.hpp
        include/services/ManapiTask.hpp
        include/ManapiUtils.hpp
        include/ManapiHttpResponse.hpp
        include/ManapiHttpRequest.hpp
        include/ManapiBigint.hpp
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
        include/ext/ManapiJsonWebTokenTraits.hpp
        include/ext/ManapiJsonWebTokenDefaults.hpp
        include/encoding/ManapiUnicode.hpp
        include/ManapiJsonBuilder.hpp
        include/ManapiBeforeDelete.hpp
        include/worker/ManapiBaseWorker.hpp
        include/http/ManapiBaseHttp.hpp
        include/worker/ManapiOpenSslOverTcp.hpp
        include/http/ManapiHttp1.hpp
        include/http/ManapiHttp2.hpp
        include/compress/ManapiHPack.hpp
        include/worker/ManapiHttp2Interface.hpp
        include/worker/ManapiHttp1Interface.hpp
        include/ManapiHttpMime.hpp
        include/components/ManapiAtomic.hpp
        include/worker/ManapiTcp.hpp
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
        src/compress/ManapiCompress.cpp
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
        src/worker/ManapiBaseWorker.cpp
        src/http/ManapiBaseHttp.cpp
        src/worker/ManapiOpenSslOverTcp.cpp
        src/http/ManapiHttp1.cpp
        src/http/ManapiHttp2.cpp
        src/worker/ManapiHttp2Interface.cpp
        src/ManapiHttpMime.cpp
        src/worker/ManapiTcp.cpp
        src/components/ManapiFormData.cpp
        include/components/ManapiFormData.hpp
        src/ManapiAsync.cpp
        include/ManapiAsync.hpp
        src/services/ManapiFutureTask.cpp
        include/services/ManapiFutureTask.hpp
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
        include/components/ManapiObjectPool.hpp
        src/ManapiInitTools.cpp
        include/components/ManapiFileTransferInfo.hpp
        include/components/ManapiMemoryPool.hpp
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
        src/components/ManapiObjectPool.cpp
        include/worker/ManapiHttp2Worker.hpp
        src/worker/ManapiHttp2Worker.cpp
        src/worker/ManapiHttp1Interface.cpp
        include/worker/ManapiInterfaceWorker.hpp
        src/worker/ManapiInterfaceWorker.cpp
        include/components/ManapiSlice.hpp
        src/components/ManapiSlice.cpp
        src/compress/ManapiHPack.cpp
        src/include/worker/ManapiNgHttp2Interface.hpp
        src/include/worker/ManapiNgHttp3Interface.hpp
        src/worker/ManapiNgHttp2Interface.cpp
        src/worker/ManapiNgHttp3Interface.cpp
)

if (MANAPIHTTP_BUILD_TYPE STREQUAL "exe")
    # EXE
    message(STATUS "Build type 'exe'")

    if (MANAPIHTTP_GRPC_DEPENDENCY)
        if (DEFINED protobuf_BIN_DIRS_DEBUG)
            set(Protobuf_PROTOC_EXECUTABLE ${protobuf_BIN_DIRS_DEBUG}/protoc)
        else ()
            set(Protobuf_PROTOC_EXECUTABLE ${protobuf_BIN_DIRS_RELEASE}/protoc)
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

            if (WIN32)
                add_custom_command(
                        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.cc"
                                "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.h"
                        COMMAND set "PATH=${MANAPI_grpc_LIB_DIRS};${MANAPI_protobuf_LIB_DIRS};%PATH%"
                        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                        ARGS ${FIL} -I${CMAKE_SOURCE_DIR} --grpc_out=${CMAKE_BINARY_DIR}/generated --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
                        DEPENDS ${Protobuf_PROTOC_EXECUTABLE}
                        VERBATIM
                )
            elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
                add_custom_command(
                        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.cc"
                                "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.h"
                        COMMAND export LD_LIBRARY_PATH=${MANAPI_grpc_LIB_DIRS}:${MANAPI_protobuf_LIB_DIRS}
                        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                        ARGS ${FIL} -I${CMAKE_SOURCE_DIR} --grpc_out=${CMAKE_BINARY_DIR}/generated --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
                        DEPENDS ${Protobuf_PROTOC_EXECUTABLE}
                        VERBATIM
                )
            else ()
                add_custom_command(
                        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.cc"
                                "${CMAKE_CURRENT_BINARY_DIR}/generated/${FIL_WE}.grpc.pb.h"
                        COMMAND LD_LIBRARY_PATH=${MANAPI_grpc_LIB_DIRS}:${MANAPI_protobuf_LIB_DIRS} ${Protobuf_PROTOC_EXECUTABLE}
                        ARGS ${FIL} -I${CMAKE_SOURCE_DIR} --grpc_out=${CMAKE_BINARY_DIR}/generated --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
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
    add_executable(${PROJECT_NAME} main.cpp ${MANAPIHTTP_HTTP_FILES} ${PROTO_SRCS} ${PROTO_HDRS})
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
        target_link_libraries(${PROJECT_NAME} PUBLIC PostgreSQL::PostgreSQL cpptrace::cpptrace)
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

    add_compile_definitions(MANAPIHTTP_CPPTRACE_DEPENDENCY=True)
else ()
    target_link_libraries       (${PROJECT_NAME} PUBLIC PkgConfig::LIBUV)
    target_include_directories  (${PROJECT_NAME} PUBLIC ${LIBUV_INCLUDE_DIRS})

    if (MANAPIHTTP_GMP_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${GMP_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${GMP_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_ZLIB_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${ZLIB_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${ZLIB_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_CURL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${CURL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${CURL_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${OPENSSL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${OPENSSL_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${WOLFSSL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${WOLFSSL_INCLUDE_DIRS})
    endif ()


    if (MANAPIHTTP_QUICHE_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${QUICHE_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${QUICHE_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_MSQUIC_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${MSQUIC_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${MSQUIC_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_TQUIC_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${TQUIC_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${TQUIC_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_BROTLI_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${Brotli_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${Brotli_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_ZSTD_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${ZSTD_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${ZSTD_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_GRPC_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${GRPC_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PUBLIC ${GRPC_INCLUDE_DIRS})
    endif ()

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