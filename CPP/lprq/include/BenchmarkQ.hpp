/******************************************************************************
 * Copyright (c) 2016, Pedro Ramalhete, Andreia Correia
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Concurrency Freaks nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************
 */
#pragma once

#include <atomic>
#include <chrono>
#include <thread>
#include <barrier>
#include <string>
#include <optional>
#include <vector>
#include <set>
#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <regex>
#include <AdditionalWork.hpp>
#include <Stats.hpp>
#include "ThreadGroup.hpp"
#include "MetaprogrammingUtils.hpp"
#include "MichaelScottQueue.hpp"
#include "FAAArrayQueue.hpp"
#include "LCRQueue.hpp"
#include "LPRQueue0.hpp"
#include "LPRQueue2.hpp"
#include "LPRQueue3.hpp"
#include "FakeLCRQueue.hpp"


using namespace std;
using namespace chrono;


namespace bench {
/*
 * We want to keep ring size as compile-time constant for queue implementations, because it enables us to store
 * the array of cells in a node without additional indirection. But we also want to experiment with different
 * ring sizes. To make it possible we define ring size as a queue implementation template parameter and instantiate
 * each queue for all possible ring sizes at compile-time. Then we use a bit of metaprogramming to choose
 * the right instance at runtime.
 *
 * The drawback of this approach is a radical increase in compilation time :(
 * This drawback can be mitigated if we make possible ring sizes configurable with cmake.
 */
using RingSizes = mpg::ParameterSet<size_t>::Of<16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384>;

template <class V, size_t ring_size>
using Queues = mpg::TypeSet<
        FAAArrayQueue<V, false, ring_size>,
        LCRQueue<V, false, ring_size>,
        FakeLCRQueue<V, false, ring_size>,
        LPRQueue0<V, false, ring_size>,
        LPRQueue2<V, false, ring_size>,
        LPRQueue3<V, false, ring_size>
>;

namespace {

static void printThroughputSummary(const Stats<long double> stats, const std::string_view unit) {
    cout << "Mean " << unit << " = " << static_cast<uint64_t>(stats.mean)
         << "; Stddev = " << static_cast<uint64_t>(stats.stddev) << endl;
}

static void printMetrics(const vector<Metrics>& metrics) {
    cout << "Metrics:\n";
    auto mStats = metricStats(metrics.begin(), metrics.end());
    for (auto [key, value] : mStats) {
        cout << key << ": mean = " << static_cast<uint64_t>(value.mean)
             << "; stddev = " << static_cast<uint64_t>(value.stddev) << '\n';
    }
}

static void writeThroughputCsvHeader(std::ostream& stream) {
    // in (almost) JMH-compatible format for uniform postprocessing
    stream << "Benchmark,\"Param: queueType\",Threads,\"Param: additionalWork\",\"Param: ringSize\","
           << "Score,\"Score Error\"\n";
}

static void writeThroughputCsvData(std::ostream& stream,
                                   const std::string_view benchmark, const std::string_view queue,
                                   const int numThreads, const double additionalWork, const size_t ringSize,
                                   const Stats<long double> stats) {
    stream << benchmark << ',' << queue << ',' << numThreads << ',' << static_cast<uint64_t>(additionalWork) << ','
           << ringSize << ',' << static_cast<uint64_t>(stats.mean) << ',' << static_cast<uint64_t>(stats.stddev)
           << endl;
}

static void writeMetricCsvData(std::ostream& stream,
                               const std::string_view benchmark, const std::string_view metric,
                               const std::string_view queue,
                               const int numThreads, const double additionalWork, const size_t ringSize,
                               const Stats<long double> stats) {
    stream << benchmark << ":get" << metric << ',' << queue << ',' << numThreads << ','
           << static_cast<uint64_t>(additionalWork) << ','  << ringSize << ','
           << fixed << setprecision(2) << stats.mean << ',' << stats.stddev
           << endl;
}

static void writeMetricCsvData(std::ostream& stream,
                               const std::string_view benchmark, const std::string_view queue,
                               const int numThreads, const double additionalWork, const size_t ringSize,
                               const vector<Metrics>& metrics) {
    auto mStats = metricStats(metrics.begin(), metrics.end());
    for (auto [key, value] : mStats) {
        writeMetricCsvData(stream, benchmark, key, queue, numThreads, additionalWork, ringSize, value);
    }
}

static uint32_t gcd(uint32_t a, uint32_t b) {
    if (b == 0) {
        return a;
    } else {
        return gcd(b, a % b);
    }
}

template <class Q>
size_t drainQueueAndCountElements(Q& queue, int tid) {
    size_t cnt = 0;
    while (queue.dequeue(tid) != nullptr)
        ++cnt;
    return cnt;
}

struct UserData {
    long long seq;
    int tid;

    UserData(long long lseq, int ltid) {
        this->seq = lseq;
        this->tid = ltid;
    }

    UserData() {
        this->seq = -2;
        this->tid = -2;
    }

    UserData(const UserData& other) : seq(other.seq), tid(other.tid) {}

    bool operator<(const UserData& other) const {
        return seq < other.seq;
    }
};
}


/**
 *
 */
class SymmetricBenchmarkQ {

private:
    struct Result {
        nanoseconds nsEnq = 0ns;
        nanoseconds nsDeq = 0ns;
        long long numEnq = 0;
        long long numDeq = 0;
        long long totOpsSec = 0;

        Result() {}

        Result(const Result& other) {
            nsEnq = other.nsEnq;
            nsDeq = other.nsDeq;
            numEnq = other.numEnq;
            numDeq = other.numDeq;
            totOpsSec = other.totOpsSec;
        }

        bool operator<(const Result& other) const {
            return totOpsSec < other.totOpsSec;
        }
    };

    // Performance benchmark constants
    static const long long kNumPairsWarmup = 1'000'000LL;     // Each thread does 1M iterations as warmup

    static const long long NSEC_IN_SEC = 1000000000LL;

    size_t numThreads;
    double additionalWork;
    bool needMetrics;


    void computeSecondaryMetrics(Metrics& m) {
        m["transfersPerNode"] = m["transfers"] / m["appendNode"];
        m["wasteToAppendNodeRatio"] = m["wasteNode"] / m["appendNode"];
    }

public:
    SymmetricBenchmarkQ(size_t numThreads, double additionalWork, bool needMetrics)
            : numThreads(numThreads), additionalWork(std::max(additionalWork, 0.5)), needMetrics(needMetrics) {}


    /**
     * enqueue-dequeue pairs: in each iteration a thread executes an enqueue followed by a dequeue;
     * the benchmark executes 10^8 pairs partitioned evenly among all threads;
     */
    template<typename Q>
    vector<long double> enqDeqBenchmark(const size_t numPairs, const int numRuns, vector<Metrics>& metrics) {
        nanoseconds deltas[numThreads][numRuns];
        atomic<bool> startFlag = {false};
        Q* queue = nullptr;

        auto enqdeq_lambda = [this, &startFlag, &numPairs, &queue](const int tid) {
            UserData ud(0, 0);
            while (!startFlag.load()) {} // Spin until the startFlag is set
            // Warmup phase
            for (size_t iter = 0; iter < kNumPairsWarmup / numThreads; iter++) {
                queue->enqueue(&ud, tid);
                if (queue->dequeue(tid) == nullptr)
                    cout << "Error at warmup dequeueing iter=" << iter << "\n";
            }

            queue->resetMetrics(tid);

            // Measurement phase
            auto startBeats = steady_clock::now();
            for (size_t iter = 0; iter < numPairs / numThreads; iter++) {
                queue->enqueue(&ud, tid);
                random_additional_work(additionalWork);
                if (queue->dequeue(tid) == nullptr) cout << "Error at measurement dequeueing iter=" << iter << "\n";
                random_additional_work(additionalWork);
            }
            auto stopBeats = steady_clock::now();
            return stopBeats - startBeats;
        };

        cout << "##### " << Q::className() << " #####  \n";
        for (int irun = 0; irun < numRuns; irun++) {
            queue = new Q(numThreads);
            ThreadGroup threads{};
            for (size_t i = 0; i < numThreads; ++i)
                threads.threadWithResult(enqdeq_lambda, deltas[i][irun]);
            startFlag.store(true);
            threads.join();
            startFlag.store(false);

            metrics.emplace_back(queue->collectMetrics());

            delete (Q*) queue;
        }

        // Sum up all the time deltas of all threads so we can find the median run
        vector<long double> opsPerSec(numRuns);
        for (int irun = 0; irun < numRuns; irun++) {
            auto agg = 0ns;
            for (size_t i = 0; i < numThreads; ++i) {
                agg += deltas[i][irun];
            }

            opsPerSec[irun] = static_cast<long double>(numPairs * 2 * NSEC_IN_SEC * numThreads) / agg.count();

            ++metrics[irun]["appendNode"]; // 1 node always exists, but we reset metrics and lose it
            metrics[irun]["transfers"] += (numPairs / numThreads) * numThreads;
        }

        return opsPerSec;
    }

    template<class Q>
    void runEnqDeqBenchmark(std::ostream& csvFile, int numPairs, int numRuns) {
        vector<Metrics> metrics;
        auto res = enqDeqBenchmark<Q>(numPairs, numRuns, metrics);
        Stats<long double> sts = stats(res.begin(), res.end());
        printThroughputSummary(sts, "ops/sec");

        if (needMetrics) {
            for (Metrics& m : metrics)
                computeSecondaryMetrics(m);
            printMetrics(metrics);
            cout << endl;
        }

        writeThroughputCsvData(csvFile, "enqDeqPairs", Q::className(),
                               numThreads, additionalWork, Q::RING_SIZE, sts);

        if (needMetrics) {
            writeMetricCsvData(csvFile, "enqDeqPairs", Q::className(),
                               numThreads, additionalWork, Q::RING_SIZE, metrics);
        }
    }

public:

    static void allThroughputTests(const std::string& csvFilename,
                                   const std::regex& queueFilter,
                                   const vector<int>& threadList,
                                   const vector<double>& additionalWorkList,
                                   const set<size_t>& ringSizeList,
                                   const bool needMetrics) {
        ofstream csvFile(csvFilename);
        writeThroughputCsvHeader(csvFile);

        const int numRuns = 5;           // 10 runs for the paper

        // Enq-Deq Throughput benchmarks
        for (double additionalWork: additionalWorkList) {
            for (int nThreads: threadList) {
                const int numPairs = std::min(nThreads * 1'600'000, 10'000'000);

                SymmetricBenchmarkQ bench(nThreads, additionalWork, needMetrics);
                RingSizes::foreach([&] <size_t ring_size> () {
                    if (!ringSizeList.contains(ring_size))
                        return;

                    cout << "\n----- Enq-Deq Benchmark   numThreads=" << nThreads << "   numPairs="
                         << numPairs / 1000000LL << "M" << "   additionalWork="
                         << static_cast<uint64_t>(additionalWork) <<  "   ringSize=" << ring_size
                         << " -----" << endl;

                    Queues<UserData, ring_size>::foreach([&] <class Q> () {
                        if (!regex_match(Q::className(), queueFilter))
                            return;

                        bench.runEnqDeqBenchmark<Q>(csvFile, numPairs, numRuns);
                    });
                });
            }
        }

        csvFile.close();
    }
};

/**
 *
 */
class ProducerConsumerBenchmarkQ {

private:
    // Performance benchmark constants
    static const long long kNumPairsWarmup = 1'000'000LL;     // Each thread does 1M iterations as warmup

    static const long long NSEC_IN_SEC = 1000000000LL;

    size_t numProducers, numConsumers;
    double additionalWork;
    double producerAdditionalWork{}, consumerAdditionalWork{};
    bool balancedLoad;
    bool needMetrics;

    void computeSecondaryMetrics(Metrics& m) {
        m["transfersPerNode"] += m["transfers"] / m["appendNode"];
        m["wasteToAppendNodeRatio"] += m["wasteNode"] / m["appendNode"];
    }

public:
    ProducerConsumerBenchmarkQ(size_t numProducers, size_t numConsumers,
                               double additionalWork, bool balancedLoad,
                               bool needMetrics)
            : numProducers(numProducers), numConsumers(numConsumers), additionalWork(additionalWork),
            balancedLoad(balancedLoad), needMetrics(needMetrics) {
        if (balancedLoad) {
            size_t total = numProducers + numConsumers;
            double ref = additionalWork * 2 / total;
            producerAdditionalWork = numProducers * ref;
            consumerAdditionalWork = numConsumers * ref;
        } else {
            producerAdditionalWork = additionalWork;
            consumerAdditionalWork = additionalWork;
        }
    }


    template<typename Q>
    vector<long double> producerConsumerBenchmark(const milliseconds runDuration, const int numRuns,
                                                  vector<Metrics>& metrics) {
        pair<uint32_t, uint32_t> transferredCount[numConsumers][numRuns];
        nanoseconds deltas[numRuns];
        Q* queue = nullptr;
        barrier<> barrier(numProducers + numConsumers + 1);
        std::atomic<bool> stopFlag{false};

        auto prod_lambda = [this, &stopFlag, &queue, &barrier](const int tid) {
            UserData ud(0, 0);

            barrier.arrive_and_wait();
            // Warmup phase
            for (size_t iter = 0; iter < kNumPairsWarmup / numProducers; iter++) {
                queue->enqueue(&ud, tid);
            }

            queue->resetMetrics(tid);
            barrier.arrive_and_wait();
            // Measurement phase
            uint64_t iter = 0;
            while (!stopFlag.load()) {
                // in case of balances load slow down producers if the queue starts growing too much
                // do not check size too often because it involves hazard pointers protection
                // note, estimateSize is more or less accurate only if the queue consists of one segment
                if ((iter & ((1ull << 6) - 1)) != 0 || !balancedLoad ||
                    queue->estimateSize(tid) < Q::RING_SIZE * 3 / 4) {

                    queue->enqueue(&ud, tid);
                    ++iter;
                }
                random_additional_work(producerAdditionalWork);
            }
        };

        auto cons_lambda = [this, &stopFlag, &queue, &barrier](const int tid) {
            UserData dummy;

            barrier.arrive_and_wait();
            // Warmup phase
            for (size_t iter = 0; iter < kNumPairsWarmup / numConsumers + 1; iter++) {
                UserData* d = queue->dequeue(tid);
                if (d != nullptr && d->seq > 0)
                    // side effect to prevent DCE
                    cout << "This message must never appear; " << iter << "\n";
            }

            queue->resetMetrics(tid);
            uint32_t successfulDeqCount = 0;
            uint32_t failedDeqCount = 0;
            barrier.arrive_and_wait();
            // Measurement phase
            while (!stopFlag.load()) {
                UserData* d = queue->dequeue(tid);
                if (d != nullptr) {
                    ++successfulDeqCount;
                    if (d == &dummy)
                        // side effect to prevent DCE
                        cout << "This message will never appear \n";
                } else {
                    ++failedDeqCount;
                }
                random_additional_work(consumerAdditionalWork);
            }

            return pair{successfulDeqCount, failedDeqCount};
        };

        cout << "##### " << Q::className() << " #####  \n";
        for (int irun = 0; irun < numRuns; irun++) {
            queue = new Q(numProducers + numConsumers);
            ThreadGroup threads{};
            for (size_t i = 0; i < numProducers; ++i)
                threads.thread(prod_lambda);
            for (size_t i = 0; i < numConsumers; ++i)
                threads.threadWithResult(cons_lambda, transferredCount[i][irun]);

            stopFlag.store(false);
            barrier.arrive_and_wait(); // start warmup
            barrier.arrive_and_wait(); // start measurements
            auto startAll = steady_clock::now();
            std::this_thread::sleep_for(runDuration);
            stopFlag.store(true);
            auto endAll = steady_clock::now();

            deltas[irun] = duration_cast<nanoseconds>(endAll - startAll);

            threads.join();

            Metrics queueMetrics = queue->collectMetrics();
            queueMetrics["remainedElements"] += drainQueueAndCountElements(*queue, 0);
            metrics.emplace_back(std::move(queueMetrics));
            delete (Q*) queue;
        }

        // Sum up all the time deltas of all threads so we can find the median run
        vector<long double> transfersPerSec(numRuns);
        for (int irun = 0; irun < numRuns; irun++) {
            uint32_t totalTransfersCount = 0;
            uint32_t totalFailedDeqCount = 0;
            for (size_t i = 0; i < numConsumers; ++i) {
                totalTransfersCount += transferredCount[i][irun].first;
                totalFailedDeqCount += transferredCount[i][irun].second;
            }

            ++metrics[irun]["appendNode"]; // 1 node always exists, but we reset metrics and lose it
            metrics[irun]["transfers"] += totalTransfersCount;
            metrics[irun]["failedDequeues"] += totalFailedDeqCount;
            metrics[irun]["duration"] += deltas[irun].count();

            transfersPerSec[irun] = static_cast<long double>(totalTransfersCount * NSEC_IN_SEC) / deltas[irun].count();
        }

        return transfersPerSec;
    }

    template<class Q>
    void runProducerConsumerBenchmark(std::ostream& csvFile, milliseconds runDuration, int numRuns) {
        vector<Metrics> metrics;
        auto res = producerConsumerBenchmark<Q>(runDuration, numRuns, metrics);
        Stats<long double> sts = stats(res.begin(), res.end());
        printThroughputSummary(sts, "transfers/sec");
        if (needMetrics) {
            for (Metrics& m : metrics)
                computeSecondaryMetrics(m);
            printMetrics(metrics);
            cout << endl;
        }

        uint32_t prodConsGcd = gcd(numProducers, numConsumers);
        uint32_t prodRatio = numProducers / prodConsGcd;
        uint32_t consRatio = numConsumers / prodConsGcd;
        ostringstream benchName{};
        benchName << "producerConsumer[" << prodRatio << '/' << consRatio;
        if (balancedLoad) {
            benchName << ";balanced";
        }
        benchName << "]";

        writeThroughputCsvData(csvFile, benchName.str(), Q::className(),
                               numProducers + numConsumers, additionalWork, Q::RING_SIZE, sts);
        if (needMetrics) {
            writeMetricCsvData(csvFile, benchName.str(), Q::className(),
                               numProducers + numConsumers, additionalWork, Q::RING_SIZE, metrics);
        }
    }

public:
    static void allThroughputTests(const std::string& csvFilename,
                                   const std::regex& queueFilter,
                                   const vector<pair<int, int>>& threadList,
                                   const vector<double>& additionalWorkList,
                                   const set<size_t>& ringSizeList,
                                   const bool balancedLoad, const bool needMetrics) {
        ofstream csvFile(csvFilename);
        writeThroughputCsvHeader(csvFile);

        const int numRuns = 5;           // 10 runs for the paper
        const milliseconds runDuration = 1000ms;

        for (double additionalWork: additionalWorkList) {
            for (auto numProdCons : threadList) {
                auto numProducers = numProdCons.first;
                auto numConsumers = numProdCons.second;
                ProducerConsumerBenchmarkQ bench(numProducers, numConsumers, additionalWork, balancedLoad, needMetrics);
                RingSizes::foreach([&] <size_t ring_size> () {
                    if (!ringSizeList.contains(ring_size))
                        return;

                    cout << "\n----- Producer-Consumer Benchmark   numProducers=" << numProducers
                         << "   numConsumers=" << numConsumers << "   runDuration=" << runDuration.count() << "ms"
                         << "   additionalWork=" << static_cast<uint64_t>(additionalWork) << "   balancedLoad="
                         << std::boolalpha << balancedLoad <<  "   ringSize=" << ring_size
                         << " -----" << endl;

                    Queues<UserData, ring_size>::foreach([&]<class Q>() {
                        if (!regex_match(Q::className(), queueFilter))
                            return;

                        bench.runProducerConsumerBenchmark<Q>(csvFile, runDuration, numRuns);
                    });
                });
            }
        }

        csvFile.close();
    }
};

}
