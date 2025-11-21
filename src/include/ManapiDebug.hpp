#pragma once

#include <string>
#include <format>
#include <iostream>
#include <fstream>

#include "ManapiAsync.hpp"
#include "ManapiErrors.hpp"
#include "ManapiTime.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "std/ManapiAsyncLogger.hpp"

#ifdef _MSC_VER
#   define MANAPIHTTP_LOG(...) manapi::debug::log_ (__LINE__, __FILE__, __PRETTY_FUNCTION__, manapi::ERR_OK, __VA_ARGS__)
#   define MANAPIHTTP_LOG2(...) manapi::debug::log_ (__LINE__, __FILE__, __PRETTY_FUNCTION__, manapi::ERR_OK, __VA_ARGS__);
#   define RETHROW_MANAPIHTTP_EXCEPTION(errnum, ...) manapi::debug::error_ (__LINE__, __FILE__, __PRETTY_FUNCTION__, errnum, __VA_ARGS__)
#   define RETHROW_MANAPIHTTP_EXCEPTION2(errnum, ...) manapi::debug::error_ (__LINE__, __FILE__, __PRETTY_FUNCTION__, errnum, __VA_ARGS__)
#else
#   define MANAPIHTTP_LOG(...) manapi::debug::log_ (__LINE__, __FILE_NAME__, __PRETTY_FUNCTION__, manapi::ERR_OK, __VA_ARGS__)
#   define MANAPIHTTP_LOG2(...) manapi::debug::log_ (__LINE__, __FILE_NAME__, __PRETTY_FUNCTION__, manapi::ERR_OK, __VA_ARGS__);
#   define RETHROW_MANAPIHTTP_EXCEPTION(errnum, ...) manapi::debug::error_ (__LINE__, __FILE_NAME__, __PRETTY_FUNCTION__, errnum, __VA_ARGS__)
#   define RETHROW_MANAPIHTTP_EXCEPTION2(errnum, ...) manapi::debug::error_ (__LINE__, __FILE_NAME__, __PRETTY_FUNCTION__, errnum, __VA_ARGS__)
#endif

#define THROW_MANAPIHTTP_EXCEPTION(errnum, ...) throw RETHROW_MANAPIHTTP_EXCEPTION (errnum, __VA_ARGS__)
#define THROW_MANAPIHTTP_EXCEPTION2(errnum, ...) throw RETHROW_MANAPIHTTP_EXCEPTION2 (errnum, __VA_ARGS__)

namespace manapi::debug {
    void do_log_ (std::string_view file_name, std::string_view func, std::size_t line, err_num errnum, std::string_view data) MANAPIHTTP_NOEXCEPT;

    template <class... Args>
    void log_ (size_t line, std::string_view file_name, std::string_view func, err_num errnum, std::string_view format, Args&& ...args) MANAPIHTTP_NOEXCEPT
    {
        try {
            const std::size_t n = sizeof...(Args);
            std::string str;
            if (n)
                str = std::vformat(format, std::make_format_args(args...));
            else
                str = format;

            debug::do_log_(file_name, func, line, errnum, str);
        }
        catch (std::exception const &e) {
            manapi_log_error("%s failed due to %s", "log()", e.what());
        }
    }

    template <class... Args>
    manapi::exception error_ (size_t line, std::string_view file_name, std::string_view func, err_num errnum, std::string_view format, Args&& ...args) MANAPIHTTP_NOEXCEPT
    {
        try {
            const std::size_t n = sizeof...(Args);
            //auto msg = std::format ("[{:%H:%M:%S}][{}]: {}() ({}:{}): ", time::current_time(true), static_cast<size_t>(errnum), func, file_name, line);
            std::string information;
            if (n)
                information = std::vformat(format, std::make_format_args(args...));
            else
                information = format;

            //msg += information;

            //logger->error(manapi::logger::default_service, static_cast<int>(errnum), std::move(msg));

            return std::move(manapi::exception (errnum, std::move(information)));
        }
        catch (std::exception const &e) {
            manapi_log_error("%s failed due to %s", "error()", e.what());
        }

        return manapi::exception(ERR_RESOURCE_EXHAUSTED, {});
    }
}