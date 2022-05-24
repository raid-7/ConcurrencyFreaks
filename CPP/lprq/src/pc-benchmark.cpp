#include "BenchmarkQ.hpp"
#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>


int main(int argc, char *argv[]) {
    CLI::App app{"Queue benchmarks"};

    std::string csvFilename = "res.csv";
    app.add_option("-f,--file", csvFilename, "File to export measurement results");

    std::vector<std::pair<int, int>> numThreads = { {1, 1} };
    app.add_option("-t,--thread-groups", numThreads, "Number of threads");

    std::vector<double> additionalWork = { 0.0 };
    app.add_option("-w,--work", additionalWork, "Additional work")
        ->check(CLI::NonNegativeNumber);

    std::vector<size_t> ringSizes = { 1024 };
    app.add_option("-r,--ring-size", ringSizes, "Ring size")
            ->check(CLI::IsMember(bench::RingSizes::Values));

    bool balancedLoad = false;
    app.add_flag("-b,--balanced-load", balancedLoad, "Balanced load");

    CLI11_PARSE(app, argc, argv);

    bench::ProducerConsumerBenchmarkQ::allThroughputTests(csvFilename, numThreads, additionalWork, ringSizes,
                                                          balancedLoad);
    return 0;
}

