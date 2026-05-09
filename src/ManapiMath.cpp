#include <random>

#include "ManapiMath.hpp"
#include "./include/ManapiUtils.hpp"

long long manapi::math::binpow(long long a, int n) MANAPIHTTP_NOEXCEPT {
    long long res = 1;
    while (n != 0) {
        if (n & 1)
            res = res * a;
        a = a * a;
        n >>= 1;
    }

    return res;
}

size_t manapi::math::random(size_t nmin, size_t nmax) MANAPIHTTP_NOEXCEPT {
    std::random_device random_dev;
    std::mt19937_64 random_ng (random_dev());
    std::uniform_int_distribution<std::mt19937_64::result_type > dist (nmin, nmax);

    return dist (random_ng);
}
