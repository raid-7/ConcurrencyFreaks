#pragma once


#include <atomic>
#include "x86AtomicOps.hpp"
#include "RQCell.hpp"
#include "HazardPointers.hpp"


/**
 * <h1> LPRQ3 Queue </h1>
 */
template<typename T, bool padded_cells = true, size_t ring_size = 1024,
        size_t num_completion_counters = 32, size_t counter_concurrency_level = 8, bool padded_counters = true>
class LPRQueue3 {
private:
    using Cell = detail::PlainCell<void*, padded_cells>;
    using CounterCell = detail::PlainCell<uint64_t, padded_counters>;

    struct Node {
        std::atomic<int64_t> head  __attribute__ ((aligned (128)));
        std::atomic<int64_t> tail  __attribute__ ((aligned (128)));
        std::atomic<Node*> next    __attribute__ ((aligned (128)));
        Cell array[ring_size];
        CounterCell counters[num_completion_counters];

        Node() {
            for (size_t i = 0; i < RING_SIZE; i++) {
                array[i].val.store(nullptr, std::memory_order_relaxed);
            }
            for (size_t i = 0; i < num_completion_counters; i++) {
                array[i].val.store(0, std::memory_order_relaxed);
            }
            head.store(0, std::memory_order_relaxed);
            tail.store(0, std::memory_order_relaxed);
            next.store(nullptr, std::memory_order_relaxed);
        }
    };

    alignas(128) std::atomic<Node*> head;
    alignas(128) std::atomic<Node*> tail;

    static const int MAX_THREADS = 128;
    const int maxThreads;

    HazardPointers<Node> hp {1, maxThreads};
    const int kHpTail = 0;
    const int kHpHead = 0;


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

    inline uint64_t tail_index(uint64_t t) {
        return (t & ~(1ull << 63));
    }

    inline bool crq_is_closed(uint64_t t) {
        return (t & (1ull << 63)) != 0;
    }

    bool is_bottom(void* const value) {
        return (reinterpret_cast<uintptr_t>(value) & 1) != 0;
    }

    void* thread_local_bottom(const int tid) {
        return reinterpret_cast<void*>(static_cast<uintptr_t>((tid << 1) | 1));
    }

    void fixState(Node *lhead) {
        while (1) {
            uint64_t t = lhead->tail.fetch_add(0);
            uint64_t h = lhead->head.fetch_add(0);
            // TODO: is it ok or not to cast "t" to int64_t ?
            if (lhead->tail.load() != (int64_t)t)
                continue;
            if (h > t) {
                int64_t tmp = t;
                if (lhead->tail.compare_exchange_strong(tmp, h))
                    break;
                continue;
            }
            break;
        }
    }

    int close_crq(Node *rq, const uint64_t tailticket, const int tries) {
        if (tries < 10) {
            int64_t tmp = tailticket + 1;
            return rq->tail.compare_exchange_strong(tmp, (tailticket + 1)|(1ull<<63));
        }
        else {
            return BIT_TEST_AND_SET(&rq->tail, 63);
        }
    }

public:
    static constexpr size_t RING_SIZE = ring_size;

    LPRQueue3(int maxThreads=MAX_THREADS) : maxThreads{maxThreads} {
        // Shared object init
        Node *sentinel = new Node;
        head.store(sentinel, std::memory_order_relaxed);
        tail.store(sentinel, std::memory_order_relaxed);
    }


    ~LPRQueue3() {
        while (dequeue(0) != nullptr); // Drain the queue
        delete head.load();            // Delete the last node
    }

    static std::string className() {
        using namespace std::string_literals;
        return "LPRQueue3"s + (padded_cells ? "/ca"s : ""s);
    }

    void enqueue(T* item, const int tid) {
        int try_close = 0;
        while (true) {
            Node* ltail = hp.protectPtr(kHpTail, tail.load(), tid);
            if (ltail != tail.load()) continue;
            Node *lnext = ltail->next.load();
            if (lnext != nullptr) {  // Help advance the tail
                tail.compare_exchange_strong(ltail, lnext);
                continue;
            }

            uint64_t tailticket = ltail->tail.fetch_add(1);
            if (crq_is_closed(tailticket)) {
                Node* newNode = new Node();
                // Solo enqueue (superfluous?)
                newNode->tail.store(1, std::memory_order_relaxed);
                newNode->array[0].val.store(item, std::memory_order_relaxed);
                Node* nullnode = nullptr;
                if (ltail->next.compare_exchange_strong(nullnode, newNode)) {// Insert new ring
                    tail.compare_exchange_strong(ltail, newNode); // Advance the tail
                    hp.clearOne(kHpTail, tid);
                    return;
                }
                delete newNode;
                continue;
            }

            uint64_t epoch = headTailEpoch(tailticket);
            Cell& cell = ltail->array[headTailIndex(tailticket)];
            CounterCell& counter = ltail->counters[headTailCompletionCounterIndex(tailticket)];
            void* val = cell.val.load();
            bool enforceClose = false;

            do { // this is a fake loop for fast breaks
                if (val != nullptr)
                    break;

                void* bottom = thread_local_bottom(tid);

                void* itemnull = nullptr;
                if (!cell.val.compare_exchange_strong(itemnull, bottom))
                    break;

                do { // fake loop again
                    uint64_t headticket = ltail->head.load();
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
                        hp.clearOne(kHpTail, tid);
                        return;
                    }
                } while (false);

                cell.val.compare_exchange_strong(bottom, nullptr);
            } while (false);

            if (enforceClose || tailticket >= RING_SIZE + ltail->head.load())
                close_crq(ltail, tailticket, ++try_close);
        }
    }

    T* dequeue(const int tid) {
        Node* lhead = hp.protectPtr(kHpHead, head.load(), tid);

        // In the next expression the order of volatile reads is essential. According to the standard
        // the order of evaluation of the operands is unspecified, but compilers, we tested, preserve it.
        if ((uint64_t)lhead->head.load() >= tail_index(lhead->tail.load())) {
            // try to return empty
            Node* lnext = lhead->next.load();
            if (lnext == nullptr) {
                hp.clearOne(kHpHead, tid);
                return nullptr;  // Queue is empty
            }

            if (head.compare_exchange_strong(lhead, lnext)) {
                hp.retire(lhead, tid);
                lhead = hp.protectPtr(kHpHead, lnext, tid);
            } else {
                lhead = hp.protectPtr(kHpHead, lhead, tid);
            }
        }

        while (true) {
            Node* lhead2 = head.load();
            if (lhead != lhead2) {
                lhead = hp.protectPtr(kHpHead, lhead2, tid);
                continue;
            }

            uint64_t headticket = lhead->head.fetch_add(1);
            uint64_t epoch = headTailEpoch(headticket);
            Cell& cell = lhead->array[headTailIndex(headticket)];
            CounterCell& counter = lhead->counters[headTailCompletionCounterIndex(headticket)];

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

                if (!is_bottom(val)) {
                    if (epoch == completeEpoch)
                        cell.val.store(nullptr);
                    break;
                } else {
                    if ((r & ((1 << 8) - 1)) == 0)
                        tt = lhead->tail.load();
                    if (!crq_is_closed(tt) && tt > headticket && r < 2 * 1024) {
                        ++r;
                        continue;
                    }

                    if (cell.val.compare_exchange_strong(val, nullptr))
                        break;
                }
            }

            if (epoch == counterCompleteEpoch(counter.val.load()))
                counter.val.fetch_add(1);

            if (val != nullptr && !is_bottom(val)) {
                hp.clearOne(kHpHead, tid);
                return static_cast<T*>(val);
            }

            if (tail_index(lhead->tail.load()) <= headticket + 1) {
                fixState(lhead);
                // try to return empty
                Node* lnext = lhead->next.load();
                if (lnext == nullptr) {
                    hp.clearOne(kHpHead, tid);
                    return nullptr;  // Queue is empty
                }
                if (tail_index(lhead->tail) <= headticket + 1) {
                    if (head.compare_exchange_strong(lhead, lnext)) {
                        hp.retire(lhead, tid);
                        lhead = hp.protectPtr(kHpHead, lnext, tid);
                    } else {
                        lhead = hp.protectPtr(kHpHead, lhead, tid);
                    }
                }
            }
        }
    }
};

