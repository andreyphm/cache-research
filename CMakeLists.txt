cmake_minimum_required(VERSION 3.16)
project(cache_research LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

set(SRCS
    source/config.cpp
    source/cache_runner.cpp)

add_library(cache_research STATIC ${SRCS})
target_include_directories(cache_research PUBLIC "headers")

add_executable(cache_research.out source/main.cpp)
target_link_libraries(cache_research.out PRIVATE cache_research)

enable_testing()
add_subdirectory(tests)
