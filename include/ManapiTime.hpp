#pragma once

#include <string>
#include <chrono>


namespace manapi::time {
    template<typename T>
    auto change_time_zone (const std::chrono::time_point<T> &utc, const std::chrono::time_zone *tz) {
        return std::chrono::zoned_time{tz, utc};
    }

    template<typename T>
    auto make_current (const std::chrono::time_point<T> &utc) {
        return change_time_zone<T>(utc, std::chrono::current_zone());
    }

    inline auto current_time (bool local = true) {
        auto t = std::chrono::system_clock::now();
        if (local) {
            return make_current(t);
        }
        return change_time_zone(t, std::chrono::locate_zone("UTC"));
    }
}