#pragma once

#include <string>
#include <format>
#include <iostream>
#include <fstream>

#if !(defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__))
# include <unistd.h>
#endif

#include "ManapiErrors.hpp"
#include "ManapiTime.hpp"

#define MANAPIHTTP_LOG(msg, ...) manapi::debug::_log (__LINE__, __FILE_NAME__, __FUNCTION__, manapi::ERR_DEBUG, msg, __VA_ARGS__)
#define MANAPIHTTP_LOG2(msg) manapi::debug::_log (__LINE__, __FILE_NAME__, __FUNCTION__, manapi::ERR_DEBUG, msg);

#define RETHROW_MANAPIHTTP_EXCEPTION(errnum, msg, ...) manapi::debug::_error (__LINE__, __FILE_NAME__, __FUNCTION__, errnum, msg, __VA_ARGS__)
#define RETHROW_MANAPIHTTP_EXCEPTION2(errnum, msg, ...) manapi::debug::_error (__LINE__, __FILE_NAME__, __FUNCTION__, errnum, msg)
#define THROW_MANAPIHTTP_EXCEPTION(errnum, msg, ...) throw RETHROW_MANAPIHTTP_EXCEPTION (errnum, msg, __VA_ARGS__)
#define THROW_MANAPIHTTP_EXCEPTION2(errnum, msg, ...) throw RETHROW_MANAPIHTTP_EXCEPTION2 (errnum, msg)

namespace manapi::debug {
    template <class... Args>
    void _log (const size_t &line, const std::string &file_name, const std::string &func, const err_num &errnum, const std::string &format, Args&& ...args)
    {
        const auto head = std::format ("[{}][{}]: {}() ({}:{}): ", time::fmt_current("%H:%M:%S", true), static_cast<size_t>(errnum), func, file_name, line);
        const auto information = std::vformat(format, std::make_format_args(args...));

        std::cout << head << information << "\n";
    }

    template <class... Args>
    manapi::exception _error (const size_t &line, const std::string &file_name, const std::string &func, const err_num &errnum, const std::string &format, Args&& ...args)
    {
        const auto head = std::format ("[{}][{}]: {}() ({}:{}): ", time::fmt_current("%H:%M:%S", true), static_cast<size_t>(errnum), func, file_name, line);
        const auto information = std::vformat(format, std::make_format_args(args...));

        std::cerr << head << information << "\n";

        return std::move(manapi::exception (errnum, information));
    }

    inline size_t debug_print_memory (const std::string &title = "common")
    {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
        return {};
#else
        pid_t pid = getpid(); // Get the process ID
        std::ifstream status_file("/proc/" + std::to_string(pid) + "/status", std::ios::binary | std::ios::in);
        std::string line;
        size_t memory_usage = 0;
        if (status_file.is_open()) {
            while (std::getline(status_file, line)) {
                if (line.find("VmRSS:")!= std::string::npos) {
                    size_t start = line.find(':') + 1;
                    size_t end = line.find(" kB");
                    memory_usage = std::stoul(line.substr(start, end - start));
                    break;
                }
            }
            MANAPIHTTP_LOG("Memory usage ({}): {} MB", title, (memory_usage / 1024));
        }

        return memory_usage;
#endif
    }
}