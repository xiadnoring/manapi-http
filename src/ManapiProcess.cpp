#include "uv.h"

#include "ManapiProcess.hpp"
#include "ManapiDebug.hpp"
#include "ManapiEventLoop.hpp"
#include "./include/ManapiUtils.hpp"

manapi::sys_error::status manapi::process::set_env(std::string_view name, std::string_view value) {
    if (auto rhs = uv_os_setenv(name.data(), value.data()))
        return sys_error::status_internal("set_env:Failed to set env", rhs);
    return sys_error::status_ok();
}

manapi::sys_error::status_or<std::string> manapi::process::get_env(std::string_view name) {
    std::string result;
    std::size_t size = 16;
    result.resize(size);

    while (auto rhs = uv_os_getenv(name.data(), result.data(), &size)) {
        if (rhs == UV_ENOBUFS) {
            result.resize(size);
            continue;
        }

        return sys_error::status_internal("get_env:Failed to get env", rhs);
    }

    result.resize(size);
    return std::move(result);
}
