#include "uv.h"

#include "ManapiProcess.hpp"
#include "include/ManapiUtils.hpp"
#include "ManapiDebug.hpp"
#include "services/ManapiEventLoop.hpp"

manapi::error::status manapi::process::set_env(std::string_view name, std::string_view value) {
    if (auto rhs = uv_os_setenv(name.data(), value.data()))
        return error::status_internal("set_env:Failed to set env", {
            {"msg", manapi::ev::strerror(rhs)}, {"code", rhs}});
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

        return error::status_internal("get_env:Failed to get env", {
            {"msg", manapi::ev::strerror(rhs)}, {"code", rhs}});
    }

    result.resize(size);
    return std::move(result);
}
