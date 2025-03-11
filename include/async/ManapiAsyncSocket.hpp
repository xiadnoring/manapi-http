#pragma once

#include "../ManapiUtils.hpp"
#include "./ManapiAsyncContext.hpp"
#include "ManapiCancellation.hpp"

namespace manapi::async {
    manapi::future<int> custom_ready (std::shared_ptr<context> ctx, int flags, int fd);
    manapi::future<int> read_ready (std::shared_ptr<context> ctx, int fd);
    manapi::future<int> write_ready (std::shared_ptr<context> ctx, int fd);
    manapi::future<int> custom_ready (std::shared_ptr<context> ctx, int flags, int fd, cancellation_action cancellation);
    manapi::future<int> read_ready (std::shared_ptr<context> ctx, int fd, cancellation_action cancellation);
    manapi::future<int> write_ready (std::shared_ptr<context> ctx, int fd, cancellation_action cancellation);

}
