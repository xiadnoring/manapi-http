#include "ManapiTime.hpp"

std::chrono::zoned_time<std::chrono::system_clock::duration> manapi::time::current_time(bool local) {
    auto const t = std::chrono::system_clock::now();
    if (local)
        return make_current(t);

    return change_time_zone(t, std::chrono::locate_zone("UTC"));
}