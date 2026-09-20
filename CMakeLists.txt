cmake_minimum_required(VERSION 3.16)
project(cache_research LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SRCS
    source/main.cpp)

add_library(cache_research STATIC ${SRCS})
target_include_directories(cache_research PUBLIC "headers")

add_executable(cache_research.out source/main.cpp)
target_link_libraries(cache_research.out cache_research)

enable_testing()
add_subdirectory(tests)
