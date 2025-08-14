/**
 * @file ManapiTime.hpp
 * @brief Provides functions to work with system time
 *
 * @author Timur Zajnullin
 */

#pragma once

#include <string>
#include <chrono>

#include "./ManapiUtils.hpp"

namespace manapi::time {
    /**
     * Change time zone for the passed time_point
     *
     * @tparam T duration
     * @param utc the time point
     * @param tz time zone to change
     * @return zoned time
     */
    template<typename T>
    auto change_time_zone (const std::chrono::time_point<T> &utc, const std::chrono::time_zone *tz) {
        return std::chrono::zoned_time{tz, utc};
    }

    /**
     * set local time zone to passed time point
     *
     * @tparam T duration
     * @param utc the time point
     * @return zoned time using local time zone
     */
    template<typename T>
    auto make_current (const std::chrono::time_point<T> &utc) {
        return change_time_zone<T>(utc, std::chrono::current_zone());
    }

    /**
     * Get current zoned time
     *
     * @param local Use local time zone
     * @return zoned time
     */
    std::chrono::zoned_time<std::chrono::system_clock::duration> current_time (bool local = true);
}