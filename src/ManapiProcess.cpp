#include "uv.h"

#include "ManapiProcess.hpp"

#include "ManapiDebug.hpp"

manapi::error::status manapi::process::set_env(std::string_view name, std::string_view key) {
    if (auto rhs = uv_os_setenv(name.data(), key.data())) {
        return error::status_internal("failed to set env");
    }
    return error::status_ok();
}

manapi::error::status_or<std::string> manapi::process::get_env(std::string_view name) {
    std::string result;
    std::size_t size = 16;
    result.resize(size);

    while (auto rhs = uv_os_getenv(name.data(), result.data(), &size)) {
        if (rhs == UV_ENOBUFS) {
            result.resize(size);
            continue;
        }

        return error::status_internal("failed to get env");
    }

    result.resize(size);
    return std::move(result);
}
