/**
 * @file ManapiUtils.hpp
 * @brief Provides compilation and definition options
 *
 * @author Timur Zajnullin
 */

#pragma once

#define MANAPIHTTP_NOEXCEPT noexcept(true)
#define MANAPIHTTP_NODISCARD [[nodiscard]]
#define MANAPIHTTP_MUST_ALLOC_START {bool s_must_alloc_ = true; do{ try {
#define MANAPIHTTP_MUST_ALLOC_END s_must_alloc_ = false; } catch (std::bad_alloc const &) { usleep(10000); } } while (s_must_alloc_);}
#define MANAPIHTTP_SINCE_AT_CUSTOM(major1, minor1, patch1, major2, minor2, patch2) ((major1) > (major2) || ((major1)==(major2)&&((minor1) > (minor2) || ((minor1)==(minor2) && (patch1)>=(patch2)))))

#include "ManapiParams.hpp"