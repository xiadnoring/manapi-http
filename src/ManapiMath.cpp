#include <random>

#include "ManapiMath.hpp"
#include "./include/ManapiUtils.hpp"

int64_t manapi::math::binpow(int64_t a, int n) MANAPIHTTP_NOEXCEPT {
    int64_t res = 1;
    while (n != 0) {
        if (n & 1)
            res = res * a;
        a = a * a;
        n >>= 1;
    }

    return res;
}

size_t manapi::math::random(size_t nmin, size_t nmax) MANAPIHTTP_NOEXCEPT {
    if constexpr (sizeof (size_t) == sizeof (uint64_t)) {
        return manapi::math::random64(static_cast<uint64_t>(nmin), static_cast<uint64_t>(nmax));
    }
    else {
        return manapi::math::random32(static_cast<uint32_t>(nmin), static_cast<uint32_t>(nmax));
    }
}

uint64_t manapi::math::random64 (uint64_t nmin, uint64_t nmax) MANAPIHTTP_NOEXCEPT {
    std::random_device random_dev;
    std::mt19937_64 random_ng (random_dev());
    std::uniform_int_distribution<std::mt19937_64::result_type > dist (nmin, nmax);

    return dist (random_ng);
}

uint32_t manapi::math::random32 (uint32_t nmin, uint32_t nmax) MANAPIHTTP_NOEXCEPT {
    std::random_device random_dev;
    std::mt19937 random_ng (random_dev());
    std::uniform_int_distribution<std::mt19937::result_type > dist (nmin, nmax);

    return static_cast<uint32_t>(dist (random_ng));
}

