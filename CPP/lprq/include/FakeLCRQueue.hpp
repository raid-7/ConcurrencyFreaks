#pragma once

/******************************************************************************
 * Copyright (c) 2014-2016, Pedro Ramalhete, Andreia Correia
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


#include <atomic>
#include "RQCell.hpp"
#include "x86AtomicOps.hpp"
#include "Metrics.hpp"
#include "HazardPointers.hpp"


/**
 * <h1> Fake LCRQ Queue </h1>
 *
 * This is LCRQ-like queue which behaves like FAAQueue, i.e. always allocates new segments.
 */
template<typename T, bool padded_cells = true, size_t ring_size = 1024>
class FakeLCRQueue : public MetricsAwareBase {
private:
    using Cell = detail::CRQCell<T*, padded_cells>;

    struct Node {
        std::atomic<int64_t> head  __attribute__ ((aligned (128)));
        std::atomic<int64_t> tail  __attribute__ ((aligned (128)));
        std::atomic<Node*> next    __attribute__ ((aligned (128)));
        Cell array[ring_size];

        Node() {
            for (unsigned i = 0; i < RING_SIZE; i++) {
                array[i].val.store(nullptr, std::memory_order_relaxed);
                array[i].idx.store(i, std::memory_order_relaxed);
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


    uint64_t node_index(uint64_t i) {
        return (i & ~(1ull << 63));
    }

    uint64_t set_unsafe(uint64_t i) {
        return (i | (1ull << 63));
    }

    uint64_t node_unsafe(uint64_t i) {
        return (i & (1ull << 63));
    }

    inline uint64_t tail_index(uint64_t t) {
        return (t & ~(1ull << 63));
    }

    int crq_is_closed(uint64_t t) {
        return (t & (1ull << 63)) != 0;
    }

    void fixState(Node *lhead) {
        while (1) {
            uint64_t t = lhead->tail.fetch_add(0);
            uint64_t h = lhead->head.fetch_add(0);
            // TODO: is it ok or not to cast "t" to int64_t ?
            if (lhead->tail.load() != (int64_t)t) continue;
            if (h > t) {
                int64_t tmp = t;
                if (lhead->tail.compare_exchange_strong(tmp, h)) break;
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

    FakeLCRQueue(int maxThreads=MAX_THREADS, bool needMetrics=false)
            : MetricsAwareBase(maxThreads, needMetrics), maxThreads{maxThreads} {
        // Shared object init
        Node *sentinel = new Node;
        head.store(sentinel, std::memory_order_relaxed);
        tail.store(sentinel, std::memory_order_relaxed);
        incMetric<"appendNode">(1, 0);
    }


    ~FakeLCRQueue() {
        while (dequeue(0) != nullptr); // Drain the queue
        delete head.load();            // Delete the last node
    }

    static std::string className() {
        using namespace std::string_literals;
        return "FakeLCRQueue"s + (padded_cells ? "/ca"s : ""s);
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
            bool closed = crq_is_closed(tailticket);
            if (!closed && tailticket >= RING_SIZE) {
                closed = true;
                close_crq(ltail, tailticket, 10); // 10 means close with bit_test&set (see `close_crq`)
            }
            if (closed) {
                Node* newNode = new Node();
                // Solo enqueue (superfluous?)
                newNode->tail.store(1, std::memory_order_relaxed);
                newNode->array[0].val.store(item, std::memory_order_relaxed);
                newNode->array[0].idx.store(0, std::memory_order_relaxed);
                Node* nullnode = nullptr;
                if (ltail->next.compare_exchange_strong(nullnode, newNode)) {// Insert new ring
                    tail.compare_exchange_strong(ltail, newNode); // Advance the tail
                    hp.clearOne(kHpTail, tid);
                    incMetric<"appendNode">(1, tid);
                    return;
                }
                delete newNode;
                incMetric<"wasteNode">(1, tid);
                continue;
            }
            Cell* cell = &ltail->array[tailticket & (RING_SIZE-1)];
            uint64_t idx = cell->idx.load();
            if (cell->val.load() == nullptr) {
                if (node_index(idx) <= tailticket) {
                    // TODO: is the missing cast before "t" ok or not to add?
                    if ((!node_unsafe(idx) || ltail->head.load() < (int64_t)tailticket)) {
                        if (CAS2((void**)cell, nullptr, idx, item, tailticket)) {
                            hp.clearOne(kHpTail, tid);
                            return;
                        }
                    }
                }
            }
            if (((int64_t)(tailticket - ltail->head.load()) >= (int64_t)RING_SIZE) &&
                close_crq(ltail, tailticket, ++try_close))
                continue;
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
            Cell* cell = &lhead->array[headticket & (RING_SIZE-1)];

            int r = 0;
            uint64_t tt = 0;

            while (true) {
                uint64_t cell_idx = cell->idx.load();
                uint64_t unsafe = node_unsafe(cell_idx);
                uint64_t idx = node_index(cell_idx);
                T* val = static_cast<T*>(cell->val.load());

                if (idx > headticket) break;

                if (val != nullptr) {
                    if (idx == headticket) {
                        if (CAS2((void**)cell, val, cell_idx, nullptr, unsafe | (headticket + RING_SIZE))) {
                            hp.clearOne(kHpHead, tid);
                            return val;
                        }
                    } else {
                        if (CAS2((void**)cell, val, cell_idx, val, set_unsafe(idx))) break;
                    }
                } else {
                    if ((r & ((1ull << 10) - 1)) == 0) tt = lhead->tail.load();
                    // Optimization: try to bail quickly if queue is closed.
                    int crq_closed = crq_is_closed(tt);
                    uint64_t t = tail_index(tt);
                    if (unsafe) { // Nothing to do, move along
                        if (CAS2((void**)cell, val, cell_idx, val, unsafe | (headticket + RING_SIZE)))
                            break;
                    } else if (t < headticket + 1 || r > 200000 || crq_closed) {
                        if (CAS2((void**)cell, val, idx, val, headticket + RING_SIZE)) {
                            if (r > 200000 && tt > RING_SIZE) BIT_TEST_AND_SET(&lhead->tail, 63);
                            break;
                        }
                    } else {
                        ++r;
                    }
                }
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
