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


namespace CLI {
template <class T1, class T2>
std::istringstream& operator>>(std::istringstream &in, std::pair<T1, T2> &val) {
    char c;
    in >> val.first >> c >> val.second;
    if (c != ':') {
        throw CLI::ConversionError("Invalid pair");
    }
    return in;
}
}

// g++ -std=c++14 main.cpp -I../include
int main(int argc, char *argv[]){
    CLI::App app{"Queue benchmarks"};

    std::vector<std::pair<int, int>> numThreads = { {1, 1} };
    app.add_option("-t,--thread-groups", numThreads, "Number of threads");

    std::vector<double> additionalWork = { 0.0 };
    app.add_option("-w,--work", additionalWork, "Additional work")
        ->check(CLI::NonNegativeNumber);

    bool balancedLoad = false;
    app.add_flag("-b,--balanced-load", balancedLoad, "Balanced load");

    CLI11_PARSE(app, argc, argv);

    bench::ProducerConsumerBenchmarkQ::allThroughputTests(numThreads, additionalWork, balancedLoad);
    return 0;
}

