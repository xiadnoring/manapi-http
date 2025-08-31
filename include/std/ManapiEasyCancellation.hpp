#pragma once
#include "./ManapiCancellation.hpp"

namespace manapi::async {
    DLLExportImport manapi::async::cancellation_action timeout_cancellation (size_t milliseconds = 500);
}
