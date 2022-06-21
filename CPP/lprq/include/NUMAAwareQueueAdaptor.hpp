#pragma once

#include <utility>
#include <atomic>
#include "Metrics.hpp"
#include "Numa.hpp"

template <class Q>
class NUMAAwareQueueAdaptor {
private:
    using T = std::remove_pointer_t<decltype(std::declval<Q>().dequeue(0))>;
    static constexpr size_t clusterWaitCycles = 8 * 1024;

    Q q;
    std::atomic<unsigned> cluster{0};

    void numaBarrier() {
        unsigned node = getNumaNode();
        unsigned currentCluster;
        size_t iter = 0;
        while (node != (currentCluster = cluster.load(std::memory_order_relaxed))) {
            if (++iter >= clusterWaitCycles) {
                cluster.compare_exchange_strong(currentCluster, node);
                break;
            }
        }
    }

public:
    template<class... Args>
    explicit NUMAAwareQueueAdaptor(Args&&... args) :q(std::forward<Args>(args)...) {}

    static constexpr size_t RING_SIZE = Q::RING_SIZE;

    static std::string className() {
        using namespace std::string_literals;
        return Q::className() + "/numa";
    }

    void enqueue(T* item, const int tid) {
        numaBarrier();
        q.enqueue(item, tid);
    }

    T* dequeue(const int tid) {
        numaBarrier();
        return q.dequeue(tid);
    }

    Metrics collectMetrics() const {
        return q.collectMetrics();
    }

    void resetMetrics(int tid) {
        q.resetMetrics(tid);
    }

    void resetMetrics() {
        q.resetMetrics();
    }
};
