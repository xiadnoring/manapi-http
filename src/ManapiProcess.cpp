#include "uv.h"

#include "ManapiProcess.hpp"

#include "ManapiDebug.hpp"

void manapi::process::set_env(std::string_view name, std::string_view key) {
    if (auto rhs = uv_os_setenv(name.data(), key.data())) {
        THROW_MANAPIHTTP_EXCEPTION(ERR_OS_ENV, "failed to set env due to result {}", rhs);
    }
}

std::string manapi::process::get_env(std::string_view name) {
    std::string result;
    std::size_t size = 16;
    result.resize(size);

    while (auto rhs = uv_os_getenv(name.data(), result.data(), &size)) {
        if (rhs == UV_ENOBUFS) {
            result.resize(size);
            continue;
        }

        THROW_MANAPIHTTP_EXCEPTION(ERR_OS_ENV, "failed to get env due to result {}", rhs);
    }

    result.resize(size);
    return std::move(result);
}
