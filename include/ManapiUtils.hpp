/**
 * @file ManapiUtils.hpp
 * @brief Provides compilation and definition options
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <string>
#include <string_view>

#define MANAPIHTTP_NOEXCEPT noexcept(true)
#define MANAPIHTTP_NODISCARD [[nodiscard]]
#define MANAPIHTTP_MUST_ALLOC_START {bool s_must_alloc_ = true; do{ try {
#ifdef _WIN32
#   define MANAPIHTTP_MUST_ALLOC_END s_must_alloc_ = false; } catch (std::bad_alloc const &) { Sleep(1); } } while (s_must_alloc_);}
#else
#   define MANAPIHTTP_MUST_ALLOC_END s_must_alloc_ = false; } catch (std::bad_alloc const &) { usleep(10000); } } while (s_must_alloc_);}
#endif
#define MANAPIHTTP_SINCE_AT_CUSTOM(major1, minor1, patch1, major2, minor2, patch2) ((major1) > (major2) || ((major1)==(major2)&&((minor1) > (minor2) || ((minor1)==(minor2) && (patch1)>=(patch2)))))

#include "ManapiParams.hpp"
#include "ManapiInt.hpp"
#ifdef _WIN32
#   define NOMINMAX
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <processthreadsapi.h>
#endif

namespace manapi {
    struct text_hash
    {
        using hash_type = std::hash<std::string_view>;
        using is_transparent = void;

        std::size_t operator()(const char* str) const;
        std::size_t operator()(std::string_view str) const;
        std::size_t operator()(std::string const& str) const;
    };
}