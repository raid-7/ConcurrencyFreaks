/*
 * main.cpp
 *
 *  Created on: Apr 23, 2016
 *      Author: pramalhe
 */

#include "BenchmarkQ.hpp"
#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>


struct ThreadGroupConfiguration {
    uint32_t producers;
    uint32_t consumers;

    explicit operator std::pair<int, int>() {
        return {producers, consumers};
    }
};

namespace CLI {
std::istringstream& operator>>(std::istringstream& in, ThreadGroupConfiguration& val) {
    char c;
    in >> val.producers >> c >> val.consumers;
    if (c != ':') {
        throw CLI::ConversionError("Invalid thread group configuration");
    }
    return in;
}
}

// g++ -std=c++14 main.cpp -I../include
int main(int argc, char *argv[]){
    CLI::App app{"Queue benchmarks"};

    std::vector<std::pair<int, int>> numThreads = { {1, 1} };
    app.add_option<std::vector<std::pair<int, int>>, ThreadGroupConfiguration>(
            "-t,--thread-groups", numThreads,
            "Number of threads in format numProducers:numConsumers");

    std::vector<double> additionalWork = { 0.0 };
    app.add_option("-w,--work", additionalWork, "Additional work")
        ->check(CLI::NonNegativeNumber);

    bool balancedLoad = false;
    app.add_flag("-b,--balanced-load", balancedLoad, "Balanced load");

    CLI11_PARSE(app, argc, argv);

    bench::ProducerConsumerBenchmarkQ::allThroughputTests(numThreads, additionalWork, balancedLoad);
    return 0;
}

