#include <thread>
#include <latch>
#include <vector>
#include <ranges>
#include <algorithm>
#include <gtest/gtest.h>
#include <MetaprogrammingUtils.hpp>

#include "FAAArrayQueue.hpp"
#include "LPRQueue0.hpp"
#include "LPRQueue2.hpp"
#include "LCRQueue.hpp"
#include "FakeLCRQueue.hpp"


namespace rng = std::ranges;

template <class Q>
class QueueTest : public ::testing::Test {
public:
    Q q{};
};

using QueuesToTest = ::testing::Types<FAAArrayQueue<int>, LCRQueue<int>, LPRQueue0<int>, LPRQueue2<int>, FakeLCRQueue<int>>;
TYPED_TEST_SUITE(QueueTest, QueuesToTest);


TYPED_TEST(QueueTest, Simple) {
    TypeParam& q = this->q;
    constexpr int tid = 1;
    int x, y, z;

    EXPECT_EQ(nullptr, q.dequeue(tid));

    q.enqueue(&x, tid);
    q.enqueue(&y, tid);

    EXPECT_EQ(&x, q.dequeue(tid));
    EXPECT_EQ(&y, q.dequeue(tid));
    EXPECT_EQ(nullptr, q.dequeue(tid));

    q.enqueue(&z, tid);

    EXPECT_EQ(&z, q.dequeue(tid));
    EXPECT_EQ(nullptr, q.dequeue(tid));
}

TYPED_TEST(QueueTest, EnqDeqStress) {
    TypeParam& q = this->q;
    constexpr int tid = 1;
    int xyz[32];

    for (uint32_t i = 0; i < 10 * 2048; ++i) {
        int* v = &xyz[i % 32];
        q.enqueue(v, tid);
        EXPECT_EQ(v, q.dequeue(tid)) << ">> " << i;
    }

    EXPECT_EQ(nullptr, q.dequeue(tid));
}

TYPED_TEST(QueueTest, BatchEnqDeqStress1) {
    TypeParam& q = this->q;
    constexpr int tid = 1;
    int xyz[32];

    for (uint32_t i = 0; i < 256; ++i) {
        for (uint32_t j = 0; j < 128; ++j) {
            int* v = &xyz[j % 32];
            q.enqueue(v, tid);
        }
        for (uint32_t j = 0; j < 128; ++j) {
            int* v = &xyz[j % 32];
            EXPECT_EQ(v, q.dequeue(tid)) << ">> " << i << ' ' << j;
        }
        EXPECT_EQ(nullptr, q.dequeue(tid));
    }
}

TYPED_TEST(QueueTest, BatchEnqDeqStress2) {
    TypeParam& q = this->q;
    constexpr int tid = 1;
    int xyz[32];

    for (uint32_t i = 0; i < 10; ++i) {
        for (uint32_t j = 0; j < 2048; ++j) {
            int* v = &xyz[j % 32];
            q.enqueue(v, tid);
        }
        for (uint32_t j = 0; j < 2048; ++j) {
            int* v = &xyz[j % 32];
            EXPECT_EQ(v, q.dequeue(tid)) << ">> " << i << ' ' << j;
        }
        EXPECT_EQ(nullptr, q.dequeue(tid));
    }
}

TYPED_TEST(QueueTest, ProducerConsumer) {
    constexpr size_t numElementsPerProducer = 1'000'000;
    constexpr size_t numProducers = 3;
    constexpr size_t numConsumers = 3;

    struct UserData {
        int tid;
        uint64_t id;
    };

    using QType = typename mpg::RebindTemplate<TypeParam>::To<UserData>; // Queue<int>  ->  Queue<UserData>
    QType q{};

    std::vector<std::vector<UserData>> producerData(numProducers);
    for (size_t i = 0; i < numProducers; ++i) {
        for (size_t j = 0; j < numElementsPerProducer; ++j) {
            producerData[i].emplace_back(i, j);
        }
    }

    std::vector<std::vector<UserData>> consumerData(numConsumers);

    int tidCnt = 0;
    std::vector<std::thread> threads;
    std::latch startBarrier{numProducers + numConsumers};
    std::latch stopBarrier{numProducers + 1};
    std::atomic<bool> stopFlag{false};

    rng::transform(producerData, std::back_inserter(threads), [&](std::vector<UserData>& data) {
        return std::thread([&q, &data, &startBarrier, &stopBarrier, tid=++tidCnt] {
            startBarrier.arrive_and_wait();
            for (UserData& ud : data) {
                q.enqueue(&ud, tid);
            }
            stopBarrier.arrive_and_wait();
        });
    });

    rng::transform(consumerData, std::back_inserter(threads), [&](std::vector<UserData>& data) {
        return std::thread([&q, &data, &startBarrier, &stopFlag, tid=++tidCnt] {
            startBarrier.arrive_and_wait();
            while (!stopFlag.load()) {
                UserData* ud = q.dequeue(tid);
                if (ud)
                    data.emplace_back(*ud);
            }
        });
    });

    stopBarrier.arrive_and_wait();
    stopFlag.store(true);

    rng::for_each(threads, &std::thread::join);

    for (std::vector<UserData>& data : consumerData) {
        rng::stable_sort(data, {}, &UserData::tid);
        for (size_t i = 1; i < data.size(); ++i) {
            const UserData& d1 = data[i - 1];
            const UserData& d2 = data[i];
            if (d1.tid == d2.tid) {
                EXPECT_LT(d1.id, d2.id) << ">> " << d1.tid << ' ' << d1.id << ' ' << d2.id;
            }
        }
    }
}
