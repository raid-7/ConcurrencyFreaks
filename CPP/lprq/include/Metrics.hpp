#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <ostream>
#include "MetaprogrammingUtils.hpp"
#include "Stats.hpp"


class Metrics {
private:
    std::unordered_map<std::string_view, size_t> metrics{};

public:
    template<mpg::TemplateStringLiteral metric>
    void inc(size_t value) {
       metrics[metric] += value;
    }

    const std::unordered_map<std::string_view, size_t>& data() const {
        return metrics;
    }

    void reset() {
        metrics.clear();
    }

    Metrics& operator +=(const Metrics& other) {
        for (auto [key, value] : other.metrics) {
            metrics[key] += value;
        }
        return *this;
    }

    friend std::ostream& operator <<(std::ostream& stream, const Metrics& metrics);
};

Metrics operator +(const Metrics& a, const Metrics& b) {
    Metrics res;
    res += a;
    res += b;
    return res;
}

std::ostream& operator <<(std::ostream& stream, const Metrics& metrics) {
    for (auto [key, value] : metrics.metrics) {
        stream << key << ": " << value << '\n';
    }
    return stream;
}

template <class It>
std::unordered_map<std::string_view, Stats<double>> metricStats(const It begin, const It end) {
    std::unordered_map<std::string_view, std::vector<double>> data;
    for (It it = begin; it != end; ++it) {
        for (auto [key, value] : it->data()) {
            data[key].push_back(static_cast<double>(value));
        }
    }

    std::unordered_map<std::string_view, Stats<double>> res;
    for (const auto& [key, values] : data) {
        res[key] = stats(values.begin(), values.end());
    }
    return res;
}


class MetricsCollector {
private:
    std::vector<Metrics> tlMetrics;

public:
    explicit MetricsCollector(size_t numThreads) :tlMetrics(numThreads) {}
    MetricsCollector(const Metrics&) = delete;
    MetricsCollector(Metrics&&) = delete;
    MetricsCollector& operator=(const Metrics&) = delete;
    MetricsCollector& operator=(Metrics&&) = delete;

    template<mpg::TemplateStringLiteral metric>
    void inc(size_t value, int tid) {
        tlMetrics[tid].template inc<metric>(value);
    }

    Metrics combine() const {
        Metrics res;
        for (size_t i = 0; i < tlMetrics.size(); ++i) {
            res += tlMetrics[i];
        }
        return res;
    }

    void reset(int tid) {
        tlMetrics[tid].reset();
    }

    void reset() {
        for (size_t i = 0; i < tlMetrics.size(); ++i) {
            tlMetrics[i].reset();
        }
    }
};

class MetricsAwareBase {
private:
    MetricsCollector collector;
    bool needMetrics;

protected:
    template<mpg::TemplateStringLiteral metric>
    void incMetric(size_t value, int tid) {
        if (needMetrics) {
            collector.template inc<metric>(value, tid);
        }
    }

public:
    MetricsAwareBase(size_t numThreads, bool needMetrics)
        : collector(numThreads), needMetrics(needMetrics) {}

    Metrics collectMetrics() const {
        return collector.combine();
    }

    void resetMetrics(int tid) {
        collector.reset(tid);
    }

    void resetMetrics() {
        collector.reset();
    }
};
