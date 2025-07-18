/**
 * @file ManapiUtils.hpp
 * @brief Provides compilation and definition options
 *
 * @author Timur Zajnullin
 */

#pragma once

#define MANAPIHTTP_NOEXPECT noexcept(true)
#define MANAPIHTTP_MUST_ALLOC_START {bool s_must_alloc_ = true; do{ try {
#define MANAPIHTTP_MUST_ALLOC_END s_must_alloc_ = false; } catch (std::bad_alloc const &) { usleep(10000); } } while (s_must_alloc_);}

#include "ManapiParams.hpp"