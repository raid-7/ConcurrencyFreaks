#pragma once

#include <atomic>
#include "LinkedRingQueue.hpp"
#include "RQCell.hpp"
#include "x86AtomicOps.hpp"


template<typename T, bool padded_cells, size_t ring_size,
        size_t num_completion_counters, size_t counter_concurrency_level, bool padded_counters>
class PRQueue3 : public QueueSegmentBase<T,
        PRQueue3<T, padded_cells, ring_size, num_completion_counters, counter_concurrency_level, padded_counters>> {
private:
    using Base = QueueSegmentBase<T,
            PRQueue3<T, padded_cells, ring_size, num_completion_counters, counter_concurrency_level, padded_counters>>;

    using Cell = detail::PlainCell<void*, padded_cells>;
    using CounterCell = detail::PlainCell<uint64_t, padded_counters>;

    Cell array[ring_size];
    CounterCell counters[num_completion_counters];


    inline uint64_t headTailIndex(uint64_t c) {
        return c % RING_SIZE;
    }

    inline uint64_t headTailEpoch(uint64_t c) {
        return c / RING_SIZE;
    }

    inline uint64_t headTailCompletionCounterIndex(uint64_t c) {
        return (c / counter_concurrency_level) % (num_completion_counters / counter_concurrency_level)
               * counter_concurrency_level +
               c % counter_concurrency_level;
    }

    inline uint64_t counterCompleteEpoch(uint64_t c) {
        return c / (RING_SIZE / num_completion_counters);
    }

    bool isBottom(void* const value) {
        return (reinterpret_cast<uintptr_t>(value) & 1) != 0;
    }

    void* threadLocalBottom(const int tid) {
        return reinterpret_cast<void*>(static_cast<uintptr_t>((tid << 1) | 1));
    }

public:
    static constexpr size_t RING_SIZE = ring_size;

    PRQueue3() :Base() {
        for (size_t i = 0; i < RING_SIZE; i++) {
            array[i].val.store(nullptr, std::memory_order_relaxed);
        }
        for (size_t i = 0; i < num_completion_counters; i++) {
            counters[i].val.store(0, std::memory_order_relaxed);
        }
        Base::head.store(0, std::memory_order_relaxed);
        Base::tail.store(0, std::memory_order_relaxed);
        Base::next.store(nullptr, std::memory_order_relaxed);
    }

    static std::string className() {
        using namespace std::string_literals;
        return "PRQueue3"s + (padded_cells ? "/ca"s : ""s);
    }

    bool enqueue(T* item, [[maybe_unused]] const int tid) {
        int try_close = 0;

        while (true) {
            uint64_t tailticket = Base::tail.fetch_add(1);
            if (Base::isClosed(tailticket))
                return false;

            uint64_t epoch = headTailEpoch(tailticket);
            Cell& cell = array[headTailIndex(tailticket)];
            CounterCell& counter = counters[headTailCompletionCounterIndex(tailticket)];
            void* val = cell.val.load();
            bool enforceClose = false;

            do { // this is a fake loop for fast breaks
                if (val != nullptr)
                    break;

                void* bottom = threadLocalBottom(tid);

                void* itemnull = nullptr;
                if (!cell.val.compare_exchange_strong(itemnull, bottom))
                    break;

                do { // fake loop again
                    uint64_t headticket = Base::head.load();
                    if (headticket > tailticket)
                        break;

                    uint64_t completeEpoch = counterCompleteEpoch(counter.val.load());
                    if (completeEpoch > epoch)
                        break;

                    if (completeEpoch != epoch) {
                        enforceClose = true;
                        break;
                    }

                    if (cell.val.compare_exchange_strong(bottom, item)) {
                        return true;
                    }
                } while (false);

                cell.val.compare_exchange_strong(bottom, nullptr);
            } while (false);

            if (enforceClose || tailticket >= RING_SIZE + Base::head.load())
                if (Base::closeSegment(tailticket, ++try_close > 10))
                    return false;
        }
    }

    T* dequeue([[maybe_unused]] const int tid) {
        if (Base::isEmpty())
            return nullptr;

        while (true) {
            uint64_t headticket = Base::head.fetch_add(1);
            uint64_t epoch = headTailEpoch(headticket);
            Cell& cell = array[headTailIndex(headticket)];
            CounterCell& counter = counters[headTailCompletionCounterIndex(headticket)];

            int r = 0;
            uint64_t tt = 0;
            void* val;

            while (true) {
                uint64_t completeEpoch = counterCompleteEpoch(counter.val.load());
                val = cell.val.load();
                if (completeEpoch != epoch && completeEpoch != counterCompleteEpoch(counter.val.load()))
                    continue;

                if (val == nullptr)
                    break;

                if (!isBottom(val)) {
                    if (epoch == completeEpoch)
                        cell.val.store(nullptr);
                    break;
                } else {
                    if ((r & ((1 << 8) - 1)) == 0)
                        tt = Base::tail.load();
                    if (!Base::isClosed(tt) && tt > headticket && r < 4 * 1024) {
                        ++r;
                        continue;
                    }

                    if (cell.val.compare_exchange_strong(val, nullptr))
                        break;
                }
            }

            if (epoch == counterCompleteEpoch(counter.val.load()))
                counter.val.fetch_add(1);

            if (val != nullptr && !isBottom(val)) {
                return static_cast<T*>(val);
            }

            if (Base::tailIndex(Base::tail.load()) <= headticket + 1) {
                Base::fixState();
                return nullptr;
            }
        }
    }
};

template<typename T, bool padded_cells = false, size_t ring_size = 1024,
        size_t num_completion_counters = 32, size_t counter_concurrency_level = 8, bool padded_counters = true>
using LPRQueue3 = LinkedRingQueue<T,
    PRQueue3<T, padded_cells, ring_size, num_completion_counters, counter_concurrency_level, padded_counters>>;
