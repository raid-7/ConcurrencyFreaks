#include <thread>
#include <gtest/gtest.h>

#include "FAAArrayQueue.hpp"
#include "LPRQueue0.hpp"
#include "LPRQueue2.hpp"
#include "LCRQueue.hpp"
#include "FakeLCRQueue.hpp"


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
        EXPECT_EQ(v, q.dequeue(tid));
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
            EXPECT_EQ(v, q.dequeue(tid));
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
            if (v != q.dequeue(tid)) {
                std::cout << i << ' ' << j << std::endl;
            }
//            EXPECT_EQ(v, q.dequeue(tid));
        }
        EXPECT_EQ(nullptr, q.dequeue(tid));
    }
}
