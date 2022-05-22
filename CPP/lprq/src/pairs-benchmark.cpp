#include "BenchmarkQ.hpp"
#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>


int main(int argc, char *argv[]){
    CLI::App app{"Queue benchmarks"};

    std::string csvFilename = "res.csv";
    app.add_option("-f,--file", csvFilename, "File to export measurement results");

    std::vector<int> numThreads = { 1 };
    app.add_option("-t,--threads", numThreads, "Number of threads")
        ->check(CLI::PositiveNumber);

    std::vector<double> additionalWork = { 0.0 };
    app.add_option("-w,--work", additionalWork, "Additional work")
        ->check(CLI::NonNegativeNumber);

    CLI11_PARSE(app, argc, argv);

    bench::SymmetricBenchmarkQ::allThroughputTests(csvFilename, numThreads, additionalWork);
    return 0;
}

