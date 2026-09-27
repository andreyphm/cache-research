include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
# [GoogleTest Quickstart: CMake] For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

foreach(cache IN ITEMS ARC LFU 2Q LIRS belady)
    add_executable(${cache}_tests ${cache}_tests.cpp)
    set_target_properties(${cache}_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

    target_include_directories(${cache}_tests PRIVATE "${PROJECT_SOURCE_DIR}/headers")
    target_link_libraries(${cache}_tests PRIVATE GTest::gtest_main)

    target_compile_options(${cache}_tests PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(${cache}_tests PROPERTIES TIMEOUT 10 LABELS "${cache}")
endforeach()
