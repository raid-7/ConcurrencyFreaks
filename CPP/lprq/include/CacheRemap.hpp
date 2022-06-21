#pragma once

#include <array>

template <size_t size, size_t cell_size, size_t cache_line_size=128>
class CacheRemap {
private:
    static_assert(cache_line_size % cell_size == 0);
    static_assert(size * cell_size % cache_line_size == 0);

    static constexpr size_t cellsPerCacheLine = cache_line_size / cell_size;
    static constexpr size_t numCacheLines = size * cell_size / cache_line_size;

public:
    constexpr inline size_t operator [](size_t i) const noexcept {
        return (i % numCacheLines) * cellsPerCacheLine + i / numCacheLines;
    }
};
