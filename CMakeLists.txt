cmake_minimum_required(VERSION 3.9)
project(manapihttp VERSION 0.0.1 DESCRIPTION "Fast HTTP Server/Client Library for easy develop API-Services and Web Applications")

if (NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
else ()
    message(STATUS "This project is a top-level one")
endif ()

set(MANAPI_BUILD_MODE $ENV{MANAPI_HTTP_MODE})

find_package(PkgConfig REQUIRED)

# include finders
include(./cmake/FindLibev.cmake)
include(GNUInstallDirs)

pkg_check_modules(GMP REQUIRED gmp)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(QUIC REQUIRED quiche)
pkg_check_modules(OPENSSL REQUIRED openssl)

set(CMAKE_CXX_STANDARD 23)

set(MANAPI_HTTP_FILES
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
        includes/ManapiThreadSafe.h
        includes/ManapiHttpTypes.h
        includes/ManapiFilesystem.h
        includes/ManapiBase64.h
        includes/ManapiCompress.h
        includes/ManapiTaskFunction.h
        includes/ManapiHash.h

        src/ManapiHttp.cpp
        src/ManapiLocker.cpp
        src/ManapiThreadPool.cpp
        src/ManapiTask.cpp
        src/ManapiTaskHttp.cpp
        src/ManapiUtils.cpp
        src/ManapiHttpResponse.cpp
        src/ManapiHttpRequest.cpp
        src/ManapiJson.cpp
        src/ManapiDecimal.cpp
        src/ManapiCompress.cpp
        src/ManapiFilesystem.cpp
        src/ManapiHash.cpp
        src/ManapiTaskFunction.cpp)

set(MANAPI_INSTALL 0)

if (MANAPI_BUILD_MODE STREQUAL "debug")
    # DEBUG
    message(STATUS "Build mode 'debug'")
    add_executable(${PROJECT_NAME} main.cpp ${MANAPI_HTTP_FILES})
else ()
    # RELEASE
    set(MANAPI_INSTALL 1)

    message(STATUS "Build mode 'release' (${MANAPI_BUILD_MODE})")
    add_library(${PROJECT_NAME} SHARED ${MANAPI_HTTP_FILES})

    set_target_properties(${PROJECT_NAME} PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR})

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.pc.in ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc @ONLY)
endif ()

set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${PROJECT_VERSION})

target_link_libraries       (${PROJECT_NAME} ${GMP_LIBRARIES} ${ZLIB_LIBRARIES} ${QUIC_LIBRARIES} ${LIBEV_LIBRARIES} ${OPENSSL_LIBRARIES})
target_include_directories  (${PROJECT_NAME} PUBLIC ${GMP_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} ${QUIC_INCLUDE_DIRS} ${LIBEV_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIRS})

target_include_directories  (${PROJECT_NAME} PRIVATE includes)

if (MANAPI_INSTALL EQUAL 1)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

    install(TARGETS ${PROJECT_NAME} EXPORT ManapiHttpConfig
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY includes/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})

    install(EXPORT ManapiHttpConfig DESTINATION lib/cmake/${PROJECT_NAME})

    export(TARGETS ${PROJECT_NAME} FILE ManapiHttpConfig.cmake)
endif ()