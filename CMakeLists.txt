cmake_minimum_required(VERSION 3.4)
project(ManapiHTTP)

find_package(PkgConfig REQUIRED)

# include finders
include(./cmake/FindLibev.cmake)

pkg_check_modules(GMP REQUIRED gmp)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(QUIC REQUIRED quiche)
pkg_check_modules(OPENSSL REQUIRED openssl)

set(CMAKE_CXX_STANDARD 23)

add_executable(${PROJECT_NAME}
        includes/ManapiHttp.h
        includes/ManapiLocker.h
        includes/ManapiThreadPool.h
        includes/ManapiTask.h
        includes/ManapiTaskHttp.h
        includes/ManapiUtils.h
        includes/ManapiHttpResponse.h
        includes/ManapiHttpRequest.h
        includes/ManapiDecimal.h
        includes/ManapiJson.h

        ManapiHttp.cpp
        ManapiLocker.cpp
        ManapiThreadPool.cpp
        ManapiTask.cpp
        ManapiTaskHttp.cpp
        ManapiUtils.cpp
        ManapiHttpResponse.cpp
        ManapiHttpRequest.cpp
        ManapiJson.cpp
        ManapiDecimal.cpp

        main.cpp
        includes/ManapiBase64.h
        includes/ManapiCompress.h
        ManapiCompress.cpp
        includes/ManapiHttpTypes.h
        includes/ManapiFilesystem.h
        ManapiFilesystem.cpp
        includes/ManapiHash.h
        ManapiHash.cpp
        includes/ManapiTaskFunction.h
        ManapiTaskFunction.cpp
        includes/ManapiThreadSafe.h)

target_link_libraries       (${PROJECT_NAME} ${GMP_LIBRARIES} ${ZLIB_LIBRARIES} ${QUIC_LIBRARIES} ${LIBEV_LIBRARIES} ${OPENSSL_LIBRARIES})
target_include_directories  (${PROJECT_NAME} PUBLIC ${GMP_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} ${QUIC_INCLUDE_DIRS} ${LIBEV_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIRS})

target_include_directories  (${PROJECT_NAME} PRIVATE includes)