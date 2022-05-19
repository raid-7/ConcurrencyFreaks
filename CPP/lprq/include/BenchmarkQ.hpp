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
#include <algorithm>
#include <cassert>
#include <fstream>
#include <sstream>
#include <AdditionalWork.hpp>
#include <Stats.hpp>
#include "MichaelScottQueue.hpp"
#include "FAAArrayQueue.hpp"
#include "LCRQueue.hpp"
#include "LPRQueue0.hpp"
#include "LPRQueue2.hpp"


using namespace std;
using namespace chrono;


namespace bench {
namespace {
static void printThroughputSummary(const Stats<long double> stats, const std::string_view unit) {
    cout << "Mean " << unit << " = " << static_cast<uint64_t>(stats.mean)
         << "; Stddev = " << static_cast<uint64_t>(stats.stddev) << endl;
}

static void writeThroughputCsvHeader(std::ostream& stream) {
    // in JMH compatible format for uniform postprocessing
    stream << "Benchmark,\"Param: queueType\",Threads,\"Param: additionalWork\",Score,\"Score Error\"\n";
}

static void writeThroughputCsvData(std::ostream& stream,
                                   const std::string_view benchmark, const std::string_view queue,
                                   const int numThreads, const double additionalWork, const Stats<long double> stats) {
    stream << benchmark << ',' << queue << ',' << numThreads << ',' << static_cast<uint64_t>(additionalWork) << ','
           << static_cast<uint64_t>(stats.mean) << ',' << static_cast<uint64_t>(stats.stddev) << endl;
}

static uint32_t gcd(uint32_t a, uint32_t b) {
    if (a > 1) {
        return gcd(b % a, a);
    } else {
        return b;
    }
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

    // Contants for Ping-Pong performance benchmark
    static const int kPingPongBatch = 1000;            // Each thread starts by injecting 1k items in the queue


    static const long long NSEC_IN_SEC = 1000000000LL;

    int numThreads;
    double additionalWork;

public:
    SymmetricBenchmarkQ(int numThreads, double additionalWork)
            : numThreads(numThreads), additionalWork(std::max(additionalWork, 0.5)) {}


    /**
     * enqueue-dequeue pairs: in each iteration a thread executes an enqueue followed by a dequeue;
     * the benchmark executes 10^8 pairs partitioned evenly among all threads;
     */
    template<typename Q>
    vector<long double> enqDeqBenchmark(const long numPairs, const int numRuns) {
        nanoseconds deltas[numThreads][numRuns];
        atomic<bool> startFlag = {false};
        Q* queue = nullptr;

        auto enqdeq_lambda = [this, &startFlag, &numPairs, &queue](nanoseconds* delta, const int tid) {
            UserData ud(0, 0);
            while (!startFlag.load()) {} // Spin until the startFlag is set
            // Warmup phase
            for (long long iter = 0; iter < kNumPairsWarmup / numThreads; iter++) {
                queue->enqueue(&ud, tid);
                if (queue->dequeue(tid) == nullptr) cout << "Error at warmup dequeueing iter=" << iter << "\n";
            }
            // Measurement phase
            auto startBeats = steady_clock::now();
            for (long long iter = 0; iter < numPairs / numThreads; iter++) {
                queue->enqueue(&ud, tid);
                random_additional_work(additionalWork);
                if (queue->dequeue(tid) == nullptr) cout << "Error at measurement dequeueing iter=" << iter << "\n";
                random_additional_work(additionalWork);
            }
            auto stopBeats = steady_clock::now();
            *delta = stopBeats - startBeats;
        };

        cout << "##### " << Q::className() << " #####  \n";
        for (int irun = 0; irun < numRuns; irun++) {
            queue = new Q(numThreads);
            thread enqdeqThreads[numThreads];
            for (int tid = 0; tid < numThreads; tid++)
                enqdeqThreads[tid] = thread(enqdeq_lambda, &deltas[tid][irun], tid);
            startFlag.store(true);
            // Sleep for 2 seconds just to let the threads see the startFlag
            this_thread::sleep_for(2s);
            for (int tid = 0; tid < numThreads; tid++) enqdeqThreads[tid].join();
            startFlag.store(false);
            delete (Q*) queue;
        }

        // Sum up all the time deltas of all threads so we can find the median run
        vector<long double> opsPerSec(numRuns);
        for (int irun = 0; irun < numRuns; irun++) {
            auto agg = 0ns;
            for (int tid = 0; tid < numThreads; tid++) {
                agg += deltas[tid][irun];
            }

            opsPerSec[irun] = static_cast<long double>(numPairs * 2 * NSEC_IN_SEC * numThreads) / agg.count();
        }

        return opsPerSec;
    }


    /**
     * Start with only enqueues 100K/numThreads, wait for them to finish, then do only dequeues but only 100K/numThreads
     */
    template<typename Q>
    void burstBenchmark(const long long burstSize, const int numIters, const int numRuns) {
        Result results[numThreads][numRuns];
        atomic<bool> startEnq = {false};
        atomic<bool> startDeq = {false};
        atomic<long> barrier = {0};
        Q* queue = nullptr;

        auto burst_lambda = [this, &startEnq, &startDeq, &burstSize, &barrier, &numIters, &queue](Result* res,
                                                                                                  const int tid) {
            UserData ud(0, 0);

            // Warmup
            const long long warmupIters = 100000LL;  // Do 1M for each thread as a warmup
            for (long long iter = 0; iter < warmupIters; iter++) queue->enqueue(&ud, tid);
            for (long long iter = 0; iter < warmupIters; iter++) {
                if (queue->dequeue(tid) == nullptr) cout << "ERROR: warmup dequeued nullptr in iter=" << iter << "\n";
            }
            // Measurements
            for (int iter = 0; iter < numIters; iter++) {
                // Start with enqueues
                while (!startEnq.load()) this_thread::yield();
                auto startBeats = steady_clock::now();
                for (long long iter = 0; iter < burstSize / numThreads; iter++) {
                    queue->enqueue(&ud, tid);
                }
                auto stopBeats = steady_clock::now();
                res->nsEnq += (stopBeats - startBeats);
                if (barrier.fetch_add(1) == numThreads) cout << "ERROR: in barrier\n";
                // dequeues
                while (!startDeq.load()) this_thread::yield();
                startBeats = steady_clock::now();
                for (long long iter = 0; iter < burstSize / numThreads; iter++) {
                    if (queue->dequeue(tid) == nullptr) {
                        cout << "ERROR: dequeued nullptr in iter=" << iter << "\n";
                        assert(false);
                    }
                }
                stopBeats = steady_clock::now();
                res->nsDeq += (stopBeats - startBeats);
                if (barrier.fetch_add(1) == numThreads) cout << "ERROR: in barrier\n";
                res->numEnq += burstSize / numThreads;
                res->numDeq += burstSize / numThreads;
            }
        };

        auto startAll = steady_clock::now();
        for (int irun = 0; irun < numRuns; irun++) {
            queue = new Q(numThreads);
            if (irun == 0) cout << "##### " << queue->className() << " #####  \n";
            thread burstThreads[numThreads];
            for (int tid = 0; tid < numThreads; tid++)
                burstThreads[tid] = thread(burst_lambda, &results[tid][irun], tid);
            this_thread::sleep_for(100ms);
            for (int iter = 0; iter < numIters; iter++) {
                // enqueue round
                startEnq.store(true);
                while (barrier.load() != numThreads) this_thread::yield();
                startEnq.store(false);
                long tmp = numThreads;
                if (!barrier.compare_exchange_strong(tmp, 0)) cout << "ERROR: CAS\n";
                // dequeue round
                startDeq.store(true);
                while (barrier.load() != numThreads) this_thread::yield();
                startDeq.store(false);
                tmp = numThreads;
                if (!barrier.compare_exchange_strong(tmp, 0)) cout << "ERROR: CAS\n";
            }
            for (int tid = 0; tid < numThreads; tid++) burstThreads[tid].join();
            delete queue;
        }
        auto endAll = steady_clock::now();
        milliseconds totalMs = duration_cast<milliseconds>(endAll - startAll);

        // Accounting
        vector<Result> agg(numRuns);
        for (int irun = 0; irun < numRuns; irun++) {
            for (int tid = 0; tid < numThreads; tid++) {
                agg[irun].nsEnq += results[tid][irun].nsEnq;
                agg[irun].nsDeq += results[tid][irun].nsDeq;
                agg[irun].numEnq += results[tid][irun].numEnq;
                agg[irun].numDeq += results[tid][irun].numDeq;
            }
            agg[irun].totOpsSec = (agg[irun].numEnq + agg[irun].numDeq) * NSEC_IN_SEC /
                                  (agg[irun].nsEnq.count() + agg[irun].nsDeq.count());
        }

        // Compute the median. numRuns should be an odd number
        sort(agg.begin(), agg.end());
        Result median = agg[numRuns / 2];
        const long long NSEC_IN_SEC = 1000000000LL;
        const long long allThreadsEnqPerSec = numThreads * median.numEnq * NSEC_IN_SEC / median.nsEnq.count();
        const long long allThreadsDeqPerSec = numThreads * median.numDeq * NSEC_IN_SEC / median.nsDeq.count();

        // Printed value is the median of the number of ops per second that all threads were able to accomplish (on average)
        cout << "Enq/sec = " << allThreadsEnqPerSec << "   Deq/sec = " << allThreadsDeqPerSec <<
             "   Total = " << (median.numEnq + median.numDeq) << "   Ops/sec = " << median.totOpsSec << "\n";

        // TODO: Print csv values
    }


    template<typename Q>
    void pingPongBenchmark(const seconds testLengthSeconds, const int numRuns) {
        Result results[numThreads][numRuns];
        atomic<bool> quit = {false};
        atomic<bool> startFlag = {false};
        Q* queue = nullptr;

        auto pingpong_lambda = [&quit, &startFlag, &queue](Result* res, const int tid) {
            UserData ud(0, 0);
            nanoseconds nsEnq = 0ns;
            nanoseconds nsDeq = 0ns;
            long long numEnq = 0;
            long long numDeq = 0;
            while (!startFlag.load()) this_thread::yield();
            while (!quit.load()) {
                // Always do kPingPongBatch (1k) enqueues and measure the time
                auto startBeats = steady_clock::now();
                for (int i = 0; i < kPingPongBatch; i++) { queue->enqueue(&ud, tid); /*this_thread::sleep_for(50ms);*/ }
                auto stopBeats = steady_clock::now();
                numEnq += kPingPongBatch;
                nsEnq += (stopBeats - startBeats);
                // Do dequeues until queue is empty, measure the time, and count how many were non-null
                startBeats = steady_clock::now();
                stopBeats = startBeats;
                while (queue->dequeue(tid) != nullptr) {
                    numDeq++; /*this_thread::sleep_for(50ms);*/
                    stopBeats = steady_clock::now();
                }
                nsDeq += (stopBeats - startBeats);
            }
            res->nsEnq = nsEnq;
            res->nsDeq = nsDeq;
            res->numEnq = numEnq;
            res->numDeq = numDeq;
        };

        for (int irun = 0; irun < numRuns; irun++) {
            queue = new Q(numThreads);
            if (irun == 0) cout << "##### " << queue->className() << " #####  \n";
            thread pingpongThreads[numThreads];
            for (int tid = 0; tid < numThreads; tid++)
                pingpongThreads[tid] = thread(pingpong_lambda, &results[tid][irun], tid);
            startFlag.store(true);
            // Sleep for 20 seconds
            this_thread::sleep_for(testLengthSeconds);
            quit.store(true);
            for (int tid = 0; tid < numThreads; tid++) pingpongThreads[tid].join();
            quit.store(false);
            startFlag.store(false);
            delete queue;
        }

        // Accounting
        vector<Result> agg(numRuns);
        for (int irun = 0; irun < numRuns; irun++) {
            for (int tid = 0; tid < numThreads; tid++) {
                agg[irun].nsEnq += results[tid][irun].nsEnq;
                agg[irun].nsDeq += results[tid][irun].nsDeq;
                agg[irun].numEnq += results[tid][irun].numEnq;
                agg[irun].numDeq += results[tid][irun].numDeq;
            }
        }

        // Compute the median. numRuns should be an odd number
        sort(agg.begin(), agg.end());
        Result median = agg[numRuns / 2];
        const long long NSEC_IN_SEC = 1000000000LL;
        // Printed value is the median of the number of ops per second that all threads were able to accomplish (on average)
        cout << "Enq/sec=" << numThreads * median.numEnq * NSEC_IN_SEC / median.nsEnq.count() << "   Deq/sec="
             << numThreads * median.numDeq * NSEC_IN_SEC / median.nsDeq.count() << "   Total="
             << numThreads * (median.numEnq + median.numDeq) << "\n";
    }

    template<class Q>
    void runEnqDeqBenchmark(std::ostream& csvFile, int numPairs, int numRuns) {
        auto res = enqDeqBenchmark<Q>(numPairs, numRuns);
        Stats<long double> sts = stats(res.begin(), res.end());
        printThroughputSummary(sts, "ops/sec");
        writeThroughputCsvData(csvFile, "enqDeqPairs", Q::className(), numThreads, additionalWork, sts);
    }

public:

    static void allThroughputTests(const vector<int>& threadList, const vector<double>& additionalWorkList) {
        ofstream csvFile("res.csv");
        writeThroughputCsvHeader(csvFile);

        const int numRuns = 5;           // 5 runs for the paper

        // Enq-Deq Throughput benchmarks
        for (double additionalWork: additionalWorkList) {
            for (int nThreads: threadList) {
                const int numPairs = std::min(nThreads * 1'000'000, 10'000'000);

                SymmetricBenchmarkQ bench(nThreads, additionalWork);
                std::cout << "\n----- Enq-Deq Benchmark   numThreads=" << nThreads << "   numPairs="
                          << numPairs / 1000000LL << "M" << "   additionalWork="
                          << static_cast<uint64_t>(additionalWork)
                          << " -----" << endl;
                bench.runEnqDeqBenchmark<FAAArrayQueue<UserData, false>>(csvFile, numPairs, numRuns);
                bench.runEnqDeqBenchmark<LCRQueue<UserData, false>>(csvFile, numPairs, numRuns);
                bench.runEnqDeqBenchmark<LPRQueue0<UserData, false>>(csvFile, numPairs, numRuns);
                bench.runEnqDeqBenchmark<LPRQueue2<UserData, false>>(csvFile, numPairs, numRuns);
            }
        }

        csvFile.close();

        // Burst Throughput benchmarks
//        const long long burstSize = 1000000LL;     // 1M for the paper
//        const int numIters = 100;                  // Number of iterations of 1M enqueues/dequeues
//        for (int nThreads: threadList) {
//            BenchmarkQ bench(nThreads, 0.);
//            std::cout << "\n----- Burst Benchmark   numThreads=" << nThreads << "   burstSize=" << burstSize / 1000LL
//                      << "K   numIters=" << numIters << " -----\n";
//            bench.burstBenchmark<MichaelScottQueue<UserData>>(burstSize, numIters, numRuns);
//            bench.burstBenchmark<LCRQueue<UserData>>(burstSize, numIters, numRuns);
//        }
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

    int numProducers, numConsumers;
    double additionalWork;
    double producerAdditionalWork{}, consumerAdditionalWork{};

public:
    ProducerConsumerBenchmarkQ(int numProducers, int numConsumers,
                               double additionalWork, bool balancedLoad)
            : numProducers(numProducers), numConsumers(numConsumers), additionalWork(additionalWork) {
        if (balancedLoad) {
            int total = numProducers + numConsumers;
            double ref = additionalWork * 2 / total;
            producerAdditionalWork = numProducers * ref;
            consumerAdditionalWork = numConsumers * ref;
        } else {
            producerAdditionalWork = additionalWork;
            consumerAdditionalWork = additionalWork;
        }
    }


    /**
     * enqueue-dequeue pairs: in each iteration a thread executes an enqueue followed by a dequeue;
     * the benchmark executes 10^8 pairs partitioned evenly among all threads;
     */
    template<typename Q>
    vector<long double> producerConsumerBenchmark(const milliseconds runDuration, const int numRuns) {
        uint32_t transferredCount[numConsumers][numRuns];
        nanoseconds deltas[numRuns];
        Q* queue = nullptr;
        barrier<> barrier(numProducers + numConsumers + 1);
        std::atomic<bool> stopFlag{false};

        auto prod_lambda = [this, &stopFlag, &queue, &barrier](const int tid) {
            UserData ud(0, 0);

            barrier.arrive_and_wait();
            // Warmup phase
            for (long long iter = 0; iter < kNumPairsWarmup / numProducers; iter++) {
                queue->enqueue(&ud, tid);
            }

            barrier.arrive_and_wait();
            // Measurement phase
            while (!stopFlag.load()) {
                queue->enqueue(&ud, tid);
                random_additional_work(producerAdditionalWork);
            }
        };

        auto cons_lambda = [this, &stopFlag, &queue, &barrier](uint32_t* cnt, const int tid) {
            barrier.arrive_and_wait();
            // Warmup phase
            for (long long iter = 0; iter < kNumPairsWarmup / numConsumers + 1; iter++) {
                UserData* d = queue->dequeue(tid);
                if (d != nullptr && d->seq > 0)
                    // side effect to prevent DCE
                    cout << "This message must never appear; " << iter << "\n";
            }

            uint32_t deqCount = 0;
            barrier.arrive_and_wait();
            // Measurement phase
            while (!stopFlag.load()) {
                UserData* d = queue->dequeue(tid);
                if (d != nullptr) {
                    ++deqCount;
                    if (d->seq > 0)
                        // side effect to prevent DCE
                        cout << "This message must never appear \n";
                }
            }

            *cnt = deqCount;
        };

        cout << "##### " << Q::className() << " #####  \n";
        for (int irun = 0; irun < numRuns; irun++) {
            queue = new Q(numProducers + numConsumers);
            thread prodConsThreads[numProducers + numConsumers];
            for (int tid = 0; tid < numProducers; tid++)
                prodConsThreads[tid] = thread(prod_lambda, tid);
            for (int tid = numProducers; tid < numProducers + numConsumers; tid++)
                prodConsThreads[tid] = thread(cons_lambda, &transferredCount[tid - numProducers][irun], tid);

            barrier.arrive_and_wait(); // start warmup
            barrier.arrive_and_wait(); // start measurements
            auto startAll = steady_clock::now();
            std::this_thread::sleep_for(runDuration);
            stopFlag.store(true);
            auto endAll = steady_clock::now();

            deltas[irun] = duration_cast<nanoseconds>(endAll - startAll);

            for (int tid = 0; tid < numProducers + numConsumers; tid++) {
                prodConsThreads[tid].join();
            }
            delete (Q*) queue;
        }

        // Sum up all the time deltas of all threads so we can find the median run
        vector<long double> transfersPerSec(numRuns);
        for (int irun = 0; irun < numRuns; irun++) {
            uint32_t totalCount = 0;
            for (int tid = 0; tid < numConsumers; tid++) {
                totalCount += transferredCount[tid][irun];
            }

            transfersPerSec[irun] = static_cast<long double>(totalCount * NSEC_IN_SEC) / deltas[irun].count();
        }

        return transfersPerSec;
    }

    template<class Q>
    void runProducerConsumerBenchmark(std::ostream& csvFile, milliseconds runDuration, int numRuns) {
        auto res = producerConsumerBenchmark<Q>(runDuration, numRuns);
        Stats<long double> sts = stats(res.begin(), res.end());
        printThroughputSummary(sts, "transfers/sec");

        uint32_t prodConsGcd = gcd(numProducers, numConsumers);
        uint32_t prodRatio = numProducers / prodConsGcd;
        uint32_t consRatio = numConsumers / prodConsGcd;
        ostringstream benchName{};
        benchName << "producerConsumer[" << prodRatio << '/' << consRatio;
        if (producerAdditionalWork != consumerAdditionalWork) {
            benchName << ";balanced";
        }
        benchName << "]";

        writeThroughputCsvData(csvFile, benchName.str(), Q::className(), numProducers + numConsumers, additionalWork, sts);
    }

public:

    static void allThroughputTests(const vector<pair<int, int>>& threadList,
                                   const vector<double>& additionalWorkList,
                                   const bool balancedLoad) {
        ofstream csvFile("res.csv");
        writeThroughputCsvHeader(csvFile);

        const int numRuns = 5;           // 5 runs for the paper
        const milliseconds runDuration = 1000ms;

        for (double additionalWork: additionalWorkList) {
            for (auto [numProducers, numConsumers] : threadList) {
                ProducerConsumerBenchmarkQ bench(numProducers, numConsumers, additionalWork, balancedLoad);
                std::cout << "\n----- Enq-Deq Benchmark   numProducers=" << numProducers << "   numConsumers="
                          << numConsumers << "   runDuration=" << runDuration.count() << "ms" << "   additionalWork="
                          << static_cast<uint64_t>(additionalWork) << "   balancedLoad=" << std::boolalpha << balancedLoad
                          << " -----" << endl;
                bench.runProducerConsumerBenchmark<FAAArrayQueue<UserData, false>>(csvFile, runDuration, numRuns);
                bench.runProducerConsumerBenchmark<LCRQueue<UserData, false>>(csvFile, runDuration, numRuns);
                bench.runProducerConsumerBenchmark<LPRQueue0<UserData, false>>(csvFile, runDuration, numRuns);
                bench.runProducerConsumerBenchmark<LPRQueue2<UserData, false>>(csvFile, runDuration, numRuns);
            }
        }

        csvFile.close();
    }
};

}