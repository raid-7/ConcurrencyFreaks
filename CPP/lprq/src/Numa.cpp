#include <Numa.hpp>
#include <sched.h>

unsigned getNumaNode() {
    unsigned node;
    getcpu(nullptr, &node);
    return node;
}
