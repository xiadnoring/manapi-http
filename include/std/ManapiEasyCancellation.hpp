#pragma once
#include "./ManapiCancellation.hpp"

namespace manapi::async {
    manapi::async::cancellation_action timeout_cancellation (size_t milliseconds = 500);
}
