cmake_minimum_required(VERSION 3.9)
project(manapihttp VERSION 0.0.1 DESCRIPTION "Fast HTTP Server/Client Library for easy develop API-Services and Web Applications")

cmake_policy(SET CMP0070 NEW)

if (NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
else ()
    message(STATUS "This project is a top-level one")
endif ()

OPTION(BUILD_SHARED_LIBS "Build shared libs" OFF)
OPTION(MANAPIHTTP_JSON_DEBUG "JSON debug variables" OFF)
OPTION(MANAPIHTTP_QUICHE_DEPENDENCY "The QUIC Implementation by Cloudflare" OFF)
OPTION(MANAPIHTTP_TQUIC_DEPENDENCY "The QUIC Implementation by Tencent" OFF)
OPTION(MANAPIHTTP_MSQUIC_DEPENDENCY "The QUIC Implementation by Microsoft" OFF)
OPTION(MANAPIHTTP_OPENSSL_DEPENDENCY "TLS, QUIC and etc implementations by OpenSSL" OFF)
OPTION(MANAPIHTTP_WOLFSSL_DEPENDENCY "TLS, QUIC and etc implementations by WolfSSL" OFF)
OPTION(MANAPIHTTP_BUILD_METHOD "Build method" "default")

OPTION(CMAKE_BUILD_TYPE "Library build type" "Release")

message(STATUS "MANAPIHTTP_BUILD_METHOD: ${MANAPIHTTP_BUILD_METHOD}")
message(STATUS "The QUIC Implementation by Cloudflare: ${MANAPIHTTP_QUICHE_DEPENDENCY}")
message(STATUS "The QUIC Implementation by Tencent: ${MANAPIHTTP_TQUIC_DEPENDENCY}")
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
    find_package(jemalloc REQUIRED)

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

    find_package(PostgreSQL REQUIRED)
    find_package(cpptrace REQUIRED)
else ()
    message(STATUS "Build method: default")

    find_package(PkgConfig REQUIRED)

    # include finders
    include(./cmake/FindLibev.cmake)
    include(./cmake/FindLsQPack.cmake)

    pkg_check_modules(GMP REQUIRED gmp)
    pkg_check_modules(ZLIB REQUIRED zlib)
    pkg_check_modules(CURL REQUIRED libcurl)
    pkg_check_modules (JEMALLOC REQUIRED jemalloc)

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        pkg_check_modules(WOLFSSL REQUIRED wolfssl)
    endif ()

    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        pkg_check_modules(OPENSSL REQUIRED openssl)
    endif ()

    if (MANAPIHTTP_TQUIC_DEPENDENCY)
        pkg_check_modules(QUIC REQUIRED tquic)
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

add_compile_definitions(MANAPIHTTP_OPENSSL_DEPENDENCY=$<BOOL:${MANAPIHTTP_OPENSSL_DEPENDENCY}> MANAPIHTTP_WOLFSSL_DEPENDENCY=$<BOOL:${MANAPIHTTP_WOLFSSL_DEPENDENCY}>
        MANAPIHTTP_QUICHE_DEPENDENCY=$<BOOL:${MANAPIHTTP_QUICHE_DEPENDENCY}> MANAPIHTTP_TQUIC_DEPENDENCY=$<BOOL:${MANAPIHTTP_TQUIC_DEPENDENCY}>)

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
        include/extensions/ManapiJsonWebTokenTraits.hpp
        include/extensions/ManapiJsonWebTokenDefaults.hpp
        include/ManapiUnicode.hpp
        include/ManapiJsonBuilder.hpp
        include/ManapiBeforeDelete.hpp
        include/worker/base_worker.hpp
        include/http/base_http.hpp
        include/worker/OpenSSL_TLS.hpp
        include/http/HTTPv1_1.hpp
        include/http/HTTPv1_1.hpp
        include/http/HeaderView.hpp
        include/compress/ManapiHPack.hpp
        include/worker/HTTPv2.hpp
        include/components/SmartBuffer.hpp
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
        src/worker/base_worker.cpp
        src/http/base_http.cpp
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
        include/async/ManapiAsyncConditionVariable.hpp
        include/components/ManapiChain.hpp
        include/async/ManapiAsyncTimer.hpp
        include/crypto/ManapiAEAD.hpp
        include/crypto/ManapiHKDF.hpp
        include/crypto/ManapiCryptoUtils.hpp
        include/crypto/ManapiAES.hpp
        src/worker/QUIC_OpenSSL_TLS.cpp
        include/worker/QUIC_OpenSSL_TLS.hpp
        include/worker/QUIC_CB_Base.hpp
        src/worker/QUIC_CB_Base.cpp
        src/worker/QUIC_CB_Base.cpp
        src/worker/OpenSSL_QUIC.cpp
        include/worker/OpenSSL_QUIC.hpp
        src/worker/UDP.cpp
        include/worker/UDP.hpp
        include/worker/tools/OpenSSLTools.hpp
        include/worker/HTTPv3_clouflare_quiche.hpp
        src/worker/HTTPv3_cloudflare_quiche.cpp
        src/worker/HTTPv3_tquic.cpp
        include/worker/HTTPv3_tquic.hpp
        include/async/ManapiAsyncPromise.hpp
        include/crypto/ManapiURL.hpp
        include/ManapiErrors.hpp
        src/ManapiErrors.cpp
        include/ManapiDebug.hpp
        include/ManapiTime.hpp
        include/ManapiString.hpp
        src/ManapiString.cpp
        include/ManapiMath.hpp
        src/ManapiMath.cpp
        include/http/Utils.hpp
        src/http/Utils.cpp
        include/async/ManapiAsyncTools.hpp
        include/worker/WolfSSL_TLS.hpp
        src/worker/WolfSSL_TLS.cpp
        include/components/AtomicAsync.hpp
        include/services/ManapiEventLoop.hpp
        src/services/ManapiEventLoop.cpp
        include/async/ManapiAsyncContext.hpp
        src/async/ManapiAsyncContext.cpp
        src/async/ManapiAsyncMutex.cpp
        src/async/ManapiAsyncConditionVariable.cpp
        include/async/ManapiAsyncFileStream.hpp
        src/async/ManapiAsyncFileStream.cpp
        include/extensions/pq/AsyncPostgreClient.hpp
        include/async/ManapiAsyncSocket.hpp
        include/extensions/pq/AsyncPostgreResult.hpp
        include/extensions/pq/AsyncPostgreRow.hpp
        include/extensions/pq/AsyncPostgreField.hpp
        include/extensions/pq/AsyncPostgreValue.hpp
        include/extensions/pq/AsyncPostgreValueTypes.hpp
        include/extensions/pq/AsyncPostgreNotification.hpp
        include/async/ManapiAsyncParallelRun.hpp
        include/services/ManapiFetch2.hpp
        include/components/TimerObject.hpp
        src/components/TimerObject.cpp
        include/extensions/jemallocator.hpp
)

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

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads)

target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if (MANAPIHTTP_BUILD_METHOD STREQUAL "conan")
    target_link_libraries (${PROJECT_NAME} PRIVATE ${QUIC_LIBRARIES} jemalloc::jemalloc gmp::gmp ZLIB::ZLIB libev::libev CURL::libcurl PostgreSQL::PostgreSQL cpptrace::cpptrace)
    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PRIVATE openssl::openssl)
    endif ()

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PRIVATE wolfssl::wolfssl)
    endif ()

    if (MANAPIHTTP_QUICHE_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PRIVATE quiche::quiche)
    endif ()

    if (MANAPIHTTP_TQUIC_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PRIVATE tquic::tquic)
    endif ()

    add_compile_definitions(MANAPIHTTP_CPPTRACE_DEPENDENCY=True)
else ()
    target_link_libraries       (${PROJECT_NAME} PRIVATE ${JEMALLOC_LIBRARIES} ${GMP_LIBRARIES} ${ZLIB_LIBRARIES} ${LIBEV_LIBRARIES} ${LSQPACK_LIBRARIES} ${CURL_LIBRARIES})
    target_include_directories  (${PROJECT_NAME} PRIVATE ${JEMALLOC_INCLUDE_DIRS} ${GMP_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} ${LIBEV_INCLUDE_DIRS} ${LSQPACK_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})

    if (MANAPIHTTP_OPENSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENSSL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_WOLFSSL_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${WOLFSSL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PRIVATE ${WOLFSSL_INCLUDE_DIRS})
    endif ()


    if (MANAPIHTTP_QUICHE_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${QUIC_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PRIVATE ${QUIC_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_MSQUIC_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${MSQUIC_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PRIVATE ${MSQUIC_INCLUDE_DIRS})
    endif ()

    if (MANAPIHTTP_TQUIC_DEPENDENCY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${TQUIC_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PRIVATE ${TQUIC_INCLUDE_DIRS})
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

if (WIN32)
    # set(CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} "/FS")
endif ()

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