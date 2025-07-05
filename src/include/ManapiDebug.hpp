#pragma once

#include <string>
#include <format>
#include <iostream>
#include <fstream>

#include "ManapiErrors.hpp"
#include "ManapiTime.hpp"

#include "async/ManapiAsyncLogger.hpp"

#if _MSC_VER
#   define MANAPIHTTP_LOG(...) manapi::debug::_log (manapi::async::current()->logger(), __LINE__, __FILE__, __FUNCTION__, manapi::ERR_OK, __VA_ARGS__)
#   define MANAPIHTTP_LOG2(...) manapi::debug::_log (manapi::async::current()->logger(), __LINE__, __FILE__, __FUNCTION__, manapi::ERR_OK, __VA_ARGS__);
#   define RETHROW_MANAPIHTTP_EXCEPTION(errnum, ...) manapi::debug::_error (__LINE__, __FILE__, __FUNCTION__, errnum, __VA_ARGS__)
#   define RETHROW_MANAPIHTTP_EXCEPTION2(errnum, ...) manapi::debug::_error (__LINE__, __FILE__, __FUNCTION__, errnum, __VA_ARGS__)
#else
#   define MANAPIHTTP_LOG(...) manapi::debug::_log (manapi::async::current()->logger(), __LINE__, __FILE_NAME__, __FUNCTION__, manapi::ERR_OK, __VA_ARGS__)
#   define MANAPIHTTP_LOG2(...) manapi::debug::_log (manapi::async::current()->logger(), __LINE__, __FILE_NAME__, __FUNCTION__, manapi::ERR_OK, __VA_ARGS__);
#   define RETHROW_MANAPIHTTP_EXCEPTION(errnum, ...) manapi::debug::_error (__LINE__, __FILE_NAME__, __FUNCTION__, errnum, __VA_ARGS__)
#   define RETHROW_MANAPIHTTP_EXCEPTION2(errnum, ...) manapi::debug::_error (__LINE__, __FILE_NAME__, __FUNCTION__, errnum, __VA_ARGS__)
#endif

#define THROW_MANAPIHTTP_EXCEPTION(errnum, ...) throw RETHROW_MANAPIHTTP_EXCEPTION (errnum, __VA_ARGS__)
#define THROW_MANAPIHTTP_EXCEPTION2(errnum, ...) throw RETHROW_MANAPIHTTP_EXCEPTION2 (errnum, __VA_ARGS__)

namespace manapi::debug {
    template <class... Args>
    void _log (const std::shared_ptr<manapi::logger> &logger, size_t line, std::string file_name, std::string func, err_num errnum, std::string format, Args&& ...args)
    {
        const std::size_t n = sizeof...(Args);
        auto msg = std::format ("{}() ({}:{}): ", func, file_name, line) + (n ? std::vformat(format, std::make_format_args(args...)) : format);
        logger->debug(manapi::logger::default_service, std::move(msg));
    }

    template <class... Args>
    manapi::exception _error (size_t line, std::string file_name, std::string func, err_num errnum, std::string format, Args&& ...args)
    {
        const std::size_t n = sizeof...(Args);
        //auto msg = std::format ("[{:%H:%M:%S}][{}]: {}() ({}:{}): ", time::current_time(true), static_cast<size_t>(errnum), func, file_name, line);
        auto information = n ? std::vformat(format, std::make_format_args(args...)) : std::move(format);

        //msg += information;

        //logger->error(manapi::logger::default_service, static_cast<int>(errnum), std::move(msg));

        return std::move(manapi::exception (errnum, std::move(information)));
    }
}