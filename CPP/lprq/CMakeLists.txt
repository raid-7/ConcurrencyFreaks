cmake_policy(SET CMP0048 NEW)
project(ConcurrentQueuesResearch)
CMAKE_MINIMUM_REQUIRED(VERSION 3.13)
set(CMAKE_CXX_STANDARD 20)

find_package(Threads REQUIRED)
include(Gtest.cmake)
include(CLI11.cmake)

# -- jemalloc
find_package(PkgConfig REQUIRED)
pkg_search_module(JEMALLOC REQUIRED jemalloc)
# jemalloc --

enable_testing()

function(make_bench name sources)
    add_executable("bench-${name}" ${sources})
    target_link_libraries("bench-${name}" PRIVATE Threads::Threads CLI11::CLI11 ${JEMALLOC_LIBRARIES})
    target_include_directories("bench-${name}" PRIVATE include/ ${JEMALLOC_INCLUDE_DIRS})
    target_compile_options("bench-${name}" PRIVATE -Wall -Wextra)
endfunction()

make_bench(enq-deq "src/pairs-benchmark.cpp;src/AdditionalWork.cpp;src/Numa.cpp")
make_bench(prod-cons "src/pc-benchmark.cpp;src/AdditionalWork.cpp;src/Numa.cpp")

add_executable(tests src/test.cpp src/Numa.cpp)
target_link_libraries(tests PRIVATE Threads::Threads gtest_main)
target_include_directories(tests PRIVATE include/)
target_compile_options(tests PRIVATE -Wall -Wextra)
add_test(NAME tests COMMAND tests)
