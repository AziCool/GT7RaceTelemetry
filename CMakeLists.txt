cmake_minimum_required(VERSION 3.20)
project(gt7telemetry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)

add_executable(gt7telemetry
    src/main.cpp
    src/salsa20.cpp
)

target_include_directories(gt7telemetry PRIVATE src)
target_link_libraries(gt7telemetry PRIVATE CURL::libcurl)

target_compile_options(gt7telemetry PRIVATE -Wall -Wextra -Wpedantic)
