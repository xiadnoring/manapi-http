#pragma once

#include <string>
#include <chrono>


namespace manapi::time {
    inline std::string fmt_current (const std::string &fmt, bool local) {
        std::time_t now = std::time(0);
        std::tm *ltm;

        if (local)
            ltm = std::localtime(&now);
        else
            ltm = std::gmtime(&now);

        std::ostringstream oss;
        oss << std::put_time(ltm, fmt.data());

        return oss.str();
    }
}