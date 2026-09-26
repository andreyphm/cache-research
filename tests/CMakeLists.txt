include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(
    googletest
    URL https://codeload.github.com/google/googletest/tar.gz/refs/tags/v1.17.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

foreach(cache IN ITEMS ARC LFU 2Q belady)
    set(${cache}_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/headers" CACHE PATH
        "Directory containing ${cache}_cache.hpp")

    add_executable(${cache}_tests ${cache}_tests.cpp)
    set_target_properties(${cache}_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    target_include_directories(${cache}_tests PRIVATE "${${cache}_INCLUDE_DIR}")
    target_link_libraries(${cache}_tests PRIVATE GTest::gtest_main)

    target_compile_options(${cache}_tests PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(${cache}_tests PROPERTIES TIMEOUT 10 LABELS "${cache}")
endforeach()
