#pragma once

namespace manapi::debug {


    typedef enum {
        LOG_TRACE,
        LOG_DEBUG,
        LOG_INFO,
        LOG_WARN,
        LOG_ERROR,
        LOG_FATAL
    } log_level;

    static const char* level_strings[] = {
        "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
    };

#ifdef LOG_NO_COLOR
    static const char* level_colors[] = {
        "", "", "", "", "", ""
    };
#else
    static const char* level_colors[] = {
        "\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"
    };
#endif

    extern bool log_trace_enabled;

    void log_log(log_level level, const char* file, int line, const char* fmt, ...) MANAPIHTTP_NOEXPECT;

    // Convenience macros
#define manapi_log_trace(...) manapi::debug::log_log(manapi::debug::LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define manapi_log_debug(...) manapi::debug::log_log(manapi::debug::LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define manapi_log_info(...)  manapi::debug::log_log(manapi::debug::LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define manapi_log_warn(...)  manapi::debug::log_log(manapi::debug::LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define manapi_log_error(...) manapi::debug::log_log(manapi::debug::LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define manapi_log_fatal(...) manapi::debug::log_log(manapi::debug::LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)
}