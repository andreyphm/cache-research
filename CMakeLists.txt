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

add_executable(cache_benchmark.out
    source/benchmark_main.cpp
    source/benchmark_runner.cpp)
target_link_libraries(cache_benchmark.out PRIVATE cache_research)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

find_package(Python3 3.10 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(research
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/benchmarks/scripts/run.py"
                --binary $<TARGET_FILE:cache_benchmark.out> --rerun
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/benchmarks/scripts/report.py"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        USES_TERMINAL
        COMMENT "Run cache research and generate CSV tables and figures")
    add_dependencies(research cache_benchmark.out)
endif()
