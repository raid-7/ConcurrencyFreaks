#include <thread>
#include <gtest/gtest.h>

#include "FAAArrayQueue.hpp"
#include "LPRQueue2.hpp"
#include "LCRQueue.hpp"


template <class Q>
class QueueTest : public ::testing::Test {
public:
    Q q{};
};

using QueuesToTest = ::testing::Types<FAAArrayQueue<int>, LCRQueue<int>, LPRQueue2<int>>;
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
