#pragma once

#include <atomic>
#include "LinkedRingQueue.hpp"
#include "RQCell.hpp"
#include "x86AtomicOps.hpp"


/**
 * <h1> Fake LCRQ Queue </h1>
 *
 * This is LCRQ-like queue which behaves like FAAQueue, i.e. always allocates new segments.
 */
template<typename T, bool padded_cells, size_t ring_size>
class FakeCRQueue : public QueueSegmentBase<T, FakeCRQueue<T, padded_cells, ring_size>> {
private:
    using Base = QueueSegmentBase<T, FakeCRQueue<T, padded_cells, ring_size>>;
    using Cell = detail::CRQCell<T*, padded_cells>;

    Cell array[ring_size];
    const uint64_t startIndex;

    inline uint64_t node_index(uint64_t i) {
        return (i & ~(1ull << 63));
    }

    inline uint64_t set_unsafe(uint64_t i) {
        return (i | (1ull << 63));
    }

    inline uint64_t node_unsafe(uint64_t i) {
        return (i & (1ull << 63));
    }

public:
    static constexpr size_t RING_SIZE = ring_size;

    FakeCRQueue(uint64_t start) :Base(), startIndex(start) {
        for (uint64_t i = start; i < start + RING_SIZE; i++) {
            uint64_t j = i % RING_SIZE;
            array[j].val.store(nullptr, std::memory_order_relaxed);
            array[j].idx.store(i, std::memory_order_relaxed);
        }
        Base::head.store(start, std::memory_order_relaxed);
        Base::tail.store(start, std::memory_order_relaxed);
    }

    static std::string className() {
        using namespace std::string_literals;
        return "FakeCRQueue"s + (padded_cells ? "/ca"s : ""s);
    }

    bool enqueue(T* item, [[maybe_unused]] const int tid) {
        int try_close = 0;

        while (true) {
            uint64_t tailticket = Base::tail.fetch_add(1);
            if (Base::isClosed(tailticket))
                return false;
            if (tailticket >= startIndex + RING_SIZE) {
                Base::closeSegment(tailticket, true);
                return false;
            }

            Cell& cell = array[tailticket % RING_SIZE];
            uint64_t idx = cell.idx.load();
            if (cell.val.load() == nullptr) {
                if (node_index(idx) <= tailticket) {
                    // TODO: is the missing cast before "t" ok or not to add?
                    if ((!node_unsafe(idx) || Base::head.load() < tailticket)) {
                        if (CAS2((void**)&cell, nullptr, idx, item, tailticket)) {
                            return true;
                        }
                    }
                }
            }
            if (tailticket >= Base::head.load() + RING_SIZE) {
                if (Base::closeSegment(tailticket, ++try_close > 10))
                    return false;
            }
        }
    }

    T* dequeue([[maybe_unused]] const int tid) {
        if (Base::isEmpty())
            return nullptr;

        while (true) {
            uint64_t headticket = Base::head.fetch_add(1);
            Cell& cell = array[headticket % RING_SIZE];

            int r = 0;
            uint64_t tt = 0;

            while (true) {
                uint64_t cell_idx = cell.idx.load();
                uint64_t unsafe = node_unsafe(cell_idx);
                uint64_t idx = node_index(cell_idx);
                T* val = static_cast<T*>(cell.val.load());

                if (idx > headticket)
                    break;

                if (val != nullptr) {
                    if (idx == headticket) {
                        if (CAS2((void**)&cell, val, cell_idx, nullptr, unsafe | (headticket + RING_SIZE))) {
                            return val;
                        }
                    } else {
                        if (CAS2((void**)&cell, val, cell_idx, val, set_unsafe(idx)))
                            break;
                    }
                } else {
                    if ((r & ((1ull << 8) - 1)) == 0)
                        tt = Base::tail.load();

                    int crq_closed = Base::isClosed(tt);
                    uint64_t t = Base::tailIndex(tt);
                    if (unsafe || t < headticket + 1 || crq_closed || r > 4*1024) {
                        if (CAS2((void**)&cell, val, cell_idx, val, unsafe | (headticket + RING_SIZE)))
                            break;
                    }
                    ++r;
                }
            }

            if (Base::tailIndex(Base::tail.load()) <= headticket + 1) {
                Base::fixState();
                return nullptr;
            }
        }
    }
};

template<typename T, bool padded_cells = false, size_t ring_size = 1024>
using FakeLCRQueue = LinkedRingQueue<T, FakeCRQueue<T, padded_cells, ring_size>>;
