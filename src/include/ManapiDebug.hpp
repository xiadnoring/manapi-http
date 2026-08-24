#pragma once

#include <string>
#include <format>
#include <iostream>
#include <fstream>

#include "ManapiAsync.hpp"
#include "ManapiErrors.hpp"
#include "ManapiTime.hpp"
#include "std/ManapiContext.hpp"
#include "std/ManapiLogger.hpp"

namespace manapi::debug {
    void set_log_trace_enabled (int value) MANAPIHTTP_NOEXCEPT;

    void set_log_name_enabled (const char *name, bool enabled);
}