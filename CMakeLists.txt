cmake_minimum_required(VERSION 3.9)
project(manapihttp VERSION 0.0.1 DESCRIPTION "Fast HTTP Server/Client Library for easy develop API-Services and Web Applications")

if (NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    message(STATUS "This project has a top-level one called [${CMAKE_PROJECT_NAME}]")
else ()
    message(STATUS "This project is a top-level one")
endif ()

if (MANAPI_BUILD_METHOD STREQUAL "conan")
    message(STATUS "Build method: conan")

    find_package(gmp REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(libev REQUIRED)
    find_package(quiche REQUIRED)
    find_package(CURL REQUIRED)
else ()
    message(STATUS "Build method: default")

    find_package(PkgConfig REQUIRED)

    # include finders
    include(./cmake/FindLibev.cmake)

    pkg_check_modules(GMP REQUIRED gmp)
    pkg_check_modules(ZLIB REQUIRED zlib)
    pkg_check_modules(QUIC REQUIRED quiche)
    pkg_check_modules(OPENSSL REQUIRED openssl)
    pkg_check_modules(CURL REQUIRED libcurl)
endif ()



set(CMAKE_CXX_STANDARD 23)

set(MANAPI_HTTP_FILES
        include/ManapiHttp.h
        include/ManapiLocker.h
        include/ManapiThreadPool.h
        include/ManapiTask.h
        include/ManapiTaskHttp.h
        include/ManapiUtils.h
        include/ManapiHttpResponse.h
        include/ManapiHttpRequest.h
        include/ManapiDecimal.h
        include/ManapiJson.h
        include/ManapiThreadSafe.h
        include/ManapiHttpTypes.h
        include/ManapiFilesystem.h
        include/ManapiBase64.h
        include/ManapiCompress.h
        include/ManapiTaskFunction.h
        include/ManapiHash.h
        include/ManapiFetch.h
        include/ManapiApi.h

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
        src/ManapiTaskFunction.cpp
        src/ManapiApi.cpp
        src/ManapiFetch.cpp)

if (MANAPI_BUILD_TYPE STREQUAL "exe")
    # EXE
    message(STATUS "Build type 'exe'")
    add_executable(${PROJECT_NAME} main.cpp test.cpp test.h ${MANAPI_HTTP_FILES})
else ()
    # LIB
    message(STATUS "Build type 'lib'")

    add_library(${PROJECT_NAME} SHARED ${MANAPI_HTTP_FILES})

    set_target_properties(${PROJECT_NAME} PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR})

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PROJECT_NAME}.pc.in ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc @ONLY)
endif ()

set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${PROJECT_VERSION})

if (MANAPI_BUILD_METHOD STREQUAL "conan")
    target_link_libraries       (${PROJECT_NAME} PUBLIC ${QUIC_LIBRARIES} gmp::gmp openssl::openssl ZLIB::ZLIB libev::libev quiche::quiche  CURL::libcurl)
else ()
    target_link_libraries       (${PROJECT_NAME} PUBLIC ${GMP_LIBRARIES} ${ZLIB_LIBRARIES} ${QUIC_LIBRARIES} ${LIBEV_LIBRARIES} ${OPENSSL_LIBRARIES} ${CURL_LIBRARIES})
    target_include_directories  (${PROJECT_NAME} PUBLIC ${GMP_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} ${QUIC_INCLUDE_DIRS} ${LIBEV_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})
endif ()

# Include public headers from the folder
target_include_directories  (${PROJECT_NAME} PRIVATE include)

if (MANAPI_BUILD_TYPE STREQUAL "exe")
    # nothing
else ()
    # If we want to install library

    include(GNUInstallDirs)

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

    install(TARGETS ${PROJECT_NAME} EXPORT ManapiHttpConfig
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})

    install(EXPORT ManapiHttpConfig DESTINATION lib/cmake/${PROJECT_NAME})

    export(TARGETS ${PROJECT_NAME} FILE ManapiHttpConfig.cmake)
endif ()