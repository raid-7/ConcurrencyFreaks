#include <AdditionalWork.hpp>
#include <random>

static std::random_device random_device;
static thread_local std::minstd_rand random_engine{random_device()};
static thread_local std::uniform_real_distribution<double> random_01_distribution{};

static inline double next_double() {
    return random_01_distribution(random_engine);
}

void random_additional_work(const double mean) {
    if (mean < 1.0)
        return;
    const double ref = 1. / mean;
    while (true) {
        if (next_double() < ref)
            break;
    }
}
