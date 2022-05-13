#pragma once

#include <atomic>

namespace detail {
template<bool padded>
struct Cell;

template<>
struct Cell<true> {
    std::atomic<void*>    val;
    std::atomic<uint64_t> idx;
    uint64_t pad[14];
} __attribute__ ((aligned (128)));

template<>
struct Cell<false> {
    std::atomic<void*>    val;
    std::atomic<uint64_t> idx;
};
}
