#pragma once

#include <atomic>
#include "x86AtomicOps.hpp"
#include "HazardPointers.hpp"
#include "Metrics.hpp"


template<class T, class Segment>
class LinkedRingQueue : public MetricsAwareBase {
private:
    static constexpr int MAX_THREADS = 128;
    static constexpr int kHpTail = 0;
    static constexpr int kHpHead = 1;
    const int maxThreads;

    alignas(128) std::atomic<Segment*> head;
    alignas(128) std::atomic<Segment*> tail;

    HazardPointers<Segment> hp {2, maxThreads};

    MetricsCollector::Accessor mAppendNode = accessor("appendNode");
    MetricsCollector::Accessor mWasteNode = accessor("wasteNode");

public:
    static constexpr size_t RING_SIZE = Segment::RING_SIZE;

    explicit LinkedRingQueue(int maxThreads=MAX_THREADS)
            : MetricsAwareBase(maxThreads), maxThreads{maxThreads} {
        // Shared object init
        Segment* sentinel = new Segment(0);
        head.store(sentinel, std::memory_order_relaxed);
        tail.store(sentinel, std::memory_order_relaxed);
        mAppendNode.inc(1, 0);
    }

    ~LinkedRingQueue() {
        while (dequeue(0) != nullptr);  // Drain the queue
        delete head.load();            // Delete the last segment
    }

    static std::string className() {
        return "L" + Segment::className();
    }

    void enqueue(T* item, int tid) {
        Segment* ltail = hp.protectPtr(kHpTail, tail.load(), tid);
        while (true) {
            Segment* ltail2 = tail.load();
            if (ltail2 != ltail) {
                ltail = hp.protectPtr(kHpTail, ltail2, tid);
                continue;
            }

            Segment *lnext = ltail->next.load();
            if (lnext != nullptr) {  // Help advance the tail
                if (tail.compare_exchange_strong(ltail, lnext)) {
                    ltail = hp.protectPtr(kHpTail, lnext, tid);
                } else {
                    ltail = hp.protectPtr(kHpTail, tail.load(), tid);
                }
                continue;
            }

            if (ltail->enqueue(item, tid)) {
                hp.clearOne(kHpTail, tid);
                break;
            }

            Segment* newTail = new Segment(ltail->tailIndex(ltail->tail.load()) - 1);
            newTail->enqueue(item, tid);

            Segment* nullNode = nullptr;
            if (ltail->next.compare_exchange_strong(nullNode, newTail)) {
                tail.compare_exchange_strong(ltail, newTail);
                hp.clearOne(kHpTail, tid);
                mAppendNode.inc(1, tid);
                break;
            } else {
                delete newTail;
                mWasteNode.inc(1, tid);
            }

            ltail = hp.protectPtr(kHpTail, nullNode, tid);
        }
    }

    T* dequeue(int tid) {
        Segment* lhead = hp.protectPtr(kHpHead, head.load(), tid);
        while (true) {
            Segment* lhead2 = head.load();
            if (lhead2 != lhead) {
                lhead = hp.protectPtr(kHpHead, lhead2, tid);
                continue;
            }

            T* item = lhead->dequeue(tid);
            if (item == nullptr) {
                Segment* lnext = lhead->next.load();
                if (lnext != nullptr) {
                    item = lhead->dequeue(tid);
                    if (item == nullptr) {
                        if (head.compare_exchange_strong(lhead, lnext)) {
                            hp.retire(lhead, tid);
                            lhead = hp.protectPtr(kHpHead, lnext, tid);
                        } else {
                            lhead = hp.protectPtr(kHpHead, lhead, tid);
                        }
                        continue;
                    }
                }
            }

            hp.clearOne(kHpHead, tid);
            return item;
        }
    }

    size_t estimateSize(int tid) {
        Segment* lhead = hp.protect(kHpHead, head, tid);
        Segment* ltail = hp.protect(kHpTail, tail, tid);
        uint64_t t = ltail->tailIndex(ltail->tail.load());
        uint64_t h = lhead->head.load();
        hp.clear(tid);
        return t > h ? t - h : 0;
    }
};

template <class T, class Segment>
struct QueueSegmentBase {
protected:
    alignas(128) std::atomic<uint64_t> head{0};
    alignas(128) std::atomic<uint64_t> tail{0};
    alignas(128) std::atomic<Segment*> next{nullptr};

    inline uint64_t tailIndex(uint64_t t) const {
        return (t & ~(1ull << 63));
    }

    inline bool isClosed(uint64_t t) const {
        return (t & (1ull << 63)) != 0;
    }

    void fixState() {
        while (true) {
            uint64_t t = tail.fetch_add(0);
            uint64_t h = head.fetch_add(0);
            if (tail.load() != t) continue;
            if (h > t) {
                uint64_t tmp = t;
                if (tail.compare_exchange_strong(tmp, h))
                    break;
                continue;
            }
            break;
        }
    }

    bool closeSegment(const uint64_t tailticket, bool force) {
        if (!force) {
            uint64_t tmp = tailticket + 1;
            return tail.compare_exchange_strong(tmp, (tailticket + 1) | (1ull<<63));
        }
        else {
            return BIT_TEST_AND_SET63(&tail);
        }
    }

    inline bool isEmpty() const {
        uint64_t h = head.load();
        uint64_t t = tailIndex(tail.load());
        return h >= t;
    }

public:
    friend class LinkedRingQueue<T, Segment>;
};
