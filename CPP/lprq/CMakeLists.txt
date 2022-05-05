cmake_policy(SET CMP0048 NEW)
project(ConcurrentQueuesResearch)
CMAKE_MINIMUM_REQUIRED(VERSION 3.13)
set(CMAKE_CXX_STANDARD 20)

find_package(Threads REQUIRED)
include(Gtest.cmake)
enable_testing()

add_executable(bench src/benchmark.cpp)
target_link_libraries(bench PRIVATE Threads::Threads)
target_include_directories(bench PRIVATE include/)
target_compile_options(bench PRIVATE -Wall -Wextra)

add_executable(tests src/test.cpp)
target_link_libraries(tests PRIVATE Threads::Threads gtest_main)
target_include_directories(tests PRIVATE include/)
target_compile_options(tests PRIVATE -Wall -Wextra)
add_test(NAME tests COMMAND tests)
