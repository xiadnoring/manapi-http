/**
 * @file ManapiMath.hpp
 * @brief Provides math utilities
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <cstddef>

#include "./ManapiUtils.hpp"

/**
 * Namespace with math utilities
 */
namespace manapi::math {
    /**
     * Binary Pow Utility
     *
     * @param a the integer
     * @param n the power
     * @return a raised to the power of n
     */
    long long binpow(long long a, int n) MANAPIHTTP_NOEXCEPT;

    /**
     * Get a random integer between nmin and nmax, inclusive
     *
     * @param nmin minimum limit
     * @param nmax maximum limit
     * @return the randomly generated integer
     */
    size_t random (size_t nmin, size_t nmax) MANAPIHTTP_NOEXCEPT;
}