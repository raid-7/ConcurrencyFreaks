#include "BenchmarkQ.hpp"
#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

#include <set>
#include <vector>
#include <string>
#include <regex>


int main(int argc, char *argv[]) {
    CLI::App app{"Queue benchmarks"};

    std::string csvFilename = "res.csv";
    app.add_option("-f,--file", csvFilename, "File to export measurement results");

    std::string queueFilter = ".*";
    app.add_option("-q,--queue", queueFilter, "ECMAScript regular expression specifying queues to benchmark");

    std::vector<std::pair<int, int>> numThreads = { {1, 1} };
    app.add_option("-t,--thread-groups", numThreads, "Number of threads");

    std::vector<double> additionalWork = { 0.0 };
    app.add_option("-w,--work", additionalWork, "Additional work")
        ->check(CLI::NonNegativeNumber);

    std::set<size_t> ringSizes = { 1024 };
    app.add_option("-r,--ring-size", ringSizes, "Ring size")
            ->check(CLI::IsMember(bench::RingSizes::Values));

    bool balancedLoad = false;
    app.add_flag("-b,--balanced-load", balancedLoad, "Balanced load");

    bool needMetrics = false;
    app.add_flag("-m,--collect-metrics", needMetrics, "Collect additional metrics");

    CLI11_PARSE(app, argc, argv);

    std::regex queueFilterR = std::regex(queueFilter, std::regex::ECMAScript | std::regex::icase | std::regex::nosubs);
    bench::ProducerConsumerBenchmarkQ::allThroughputTests(csvFilename, queueFilterR, numThreads, additionalWork,
                                                          ringSizes, balancedLoad, needMetrics);
    return 0;
}

