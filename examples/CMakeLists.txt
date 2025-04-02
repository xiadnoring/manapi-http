cmake_minimum_required(VERSION 3.10)
project(Test)

set(CMAKE_CXX_STANDARD 23)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(ManapiHttp REQUIRED)

add_executable(${PROJECT_NAME} 00-main.cpp)

message(STATUS ${MANAPIHTTP_LIBRARIES})
message(STATUS ${MANAPIHTTP_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} Threads::Threads manapihttp)