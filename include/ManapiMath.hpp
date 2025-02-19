#pragma once

#include <cstddef>
#include "ManapiUtils.hpp"

namespace manapi::math {
    long long binpow(long long a, int n);
    size_t random (const size_t &_min, const size_t &_max);
}