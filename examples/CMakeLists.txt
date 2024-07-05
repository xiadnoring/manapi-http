cmake_minimum_required(VERSION 3.9)
project(Test)

set(CMAKE_CXX_STANDARD 23)

find_package(PkgConfig REQUIRED)
find_package(ManapiHttp REQUIRED)

add_executable(${PROJECT_NAME} 00-main.cpp)

target_link_libraries(${PROJECT_NAME} manapihttp)