#include "ManapiMath.hpp"

#include <random>

long long manapi::math::binpow(long long a, int n) {
    long long res = 1;
    while (n != 0) {
        if (n & 1)
            res = res * a;
        a = a * a;
        n >>= 1;
    }

    return res;
}

size_t manapi::math::random(const size_t &_min, const size_t &_max) {
    std::random_device random_dev;
    std::mt19937 random_ng (random_dev());
    std::uniform_int_distribution<std::mt19937::result_type > dist (_min, _max);

    return dist (random_ng);
}
