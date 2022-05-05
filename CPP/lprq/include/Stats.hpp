#pragma once

#include <cmath>
#include <iterator>

template <class V>
struct Stats {
    V mean;
    V stddev;
};


template <class It, class V = typename std::iterator_traits<It>::value_type>
Stats<V> stats(const It begin, const It end) {
    V sum{};
    size_t n = 0;
    for (It i = begin; i != end; ++i) {
        sum += *i;
        ++n;
    }
    V mean = sum / n;
    V sqSum{};
    for (It i = begin; i != end; ++i) {
        V x = *i - mean;
        sqSum += x * x;
    }
    V stddev = n == 1 ? V{} : std::sqrt(sqSum / (n - 1));
    return {mean, stddev};
}
