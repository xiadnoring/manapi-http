#pragma once

#include "./ManapiUtils.hpp"

namespace manapi::debug {


    typedef enum {
        LOG_TRACE,
        LOG_DEBUG,
        LOG_INFO,
        LOG_WARN,
        LOG_ERROR,
        LOG_FATAL
    } log_level;

    typedef enum {
        LOG_TRACE_NONE = 0,
        LOG_TRACE_HIGH,
        LOG_TRACE_MEDIUM,
        LOG_TRACE_LOW,
        LOG_TRACE_HARD
    } trace_level;

    void logit(log_level type, const char* file, int line, const char *name, const char* fmt, ...) MANAPIHTTP_NOEXCEPT;

    void logit(log_level type, const char* file, int line, const char *name, int level, const char* fmt, ...) MANAPIHTTP_NOEXCEPT;

    void flogit(log_level type, const char* file, const char *func, int line, const char *name, const char* fmt, ...) MANAPIHTTP_NOEXCEPT;

    void flogit(log_level type, const char* file, const char *func, int line, const char *name, int level, const char* fmt, ...) MANAPIHTTP_NOEXCEPT;

#ifndef LOGNAME
#   define LOGNAME "manapihttp"
#endif

    // Convenience macros
#define manapi_log_trace(...) manapi::debug::logit(manapi::debug::LOG_TRACE, __FILE__, __LINE__, LOGNAME, __VA_ARGS__)
#define manapi_log_trace_hard(...) manapi::debug::logit(manapi::debug::LOG_TRACE, __FILE__, __LINE__, LOGNAME, manapi::debug::LOG_TRACE_HARD, __VA_ARGS__)
#define manapi_log_debug(...) manapi::debug::logit(manapi::debug::LOG_DEBUG, __FILE__, __LINE__, LOGNAME, __VA_ARGS__)
#define manapi_log_info(...)  manapi::debug::logit(manapi::debug::LOG_INFO,  __FILE__, __LINE__, LOGNAME, __VA_ARGS__)
#define manapi_log_warn(...)  manapi::debug::logit(manapi::debug::LOG_WARN,  __FILE__, __LINE__, LOGNAME, __VA_ARGS__)
#define manapi_log_error(...) manapi::debug::logit(manapi::debug::LOG_ERROR, __FILE__, __LINE__, LOGNAME, __VA_ARGS__)
#define manapi_log_fatal(...) manapi::debug::logit(manapi::debug::LOG_FATAL, __FILE__, __LINE__, LOGNAME, __VA_ARGS__)
#define manapi_log_trace2(...) manapi::debug::logit(manapi::debug::LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define manapi_log_trace_hard2(...) manapi::debug::logit(manapi::debug::LOG_TRACE, __FILE__, __LINE__, manapi::debug::LOG_TRACE_HARD, __VA_ARGS__)
#define manapi_log_debug2(...) manapi::debug::logit(manapi::debug::LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define manapi_log_info2(...)  manapi::debug::logit(manapi::debug::LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define manapi_log_warn2(...)  manapi::debug::logit(manapi::debug::LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define manapi_log_error2(...) manapi::debug::logit(manapi::debug::LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define manapi_log_fatal2(...) manapi::debug::logit(manapi::debug::LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)
#ifdef _MSC_VER
#   define manapi_log_ferror(...) manapi::debug::flogit(manapi::debug::LOG_ERROR, __FILE__, __FUNCTION__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_ftrace(...) manapi::debug::flogit(manapi::debug::LOG_TRACE, __FILE__, __FUNCTION__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_finfo(...) manapi::debug::flogit(manapi::debug::LOG_INFO, __FILE__, __FUNCTION__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_fwarn(...) manapi::debug::flogit(manapi::debug::LOG_WARN, __FILE__, __FUNCTION__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_fdebug(...) manapi::debug::flogit(manapi::debug::LOG_DEBUG, __FILE__, __FUNCTION__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_ffatal(...) manapi::debug::flogit(manapi::debug::LOG_FATAL, __FILE__, __FUNCTION__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_ferror2(...) manapi::debug::flogit(manapi::debug::LOG_ERROR, __FILE__, __FUNCTION__, __LINE__,__VA_ARGS__)
#   define manapi_log_ftrace2(...) manapi::debug::flogit(manapi::debug::LOG_TRACE, __FILE__, __FUNCTION__, __LINE__,__VA_ARGS__)
#   define manapi_log_finfo2(...) manapi::debug::flogit(manapi::debug::LOG_INFO, __FILE__, __FUNCTION__, __LINE__,__VA_ARGS__)
#   define manapi_log_fwarn2(...) manapi::debug::flogit(manapi::debug::LOG_WARN, __FILE__, __FUNCTION__, __LINE__,__VA_ARGS__)
#   define manapi_log_fdebug2(...) manapi::debug::flogit(manapi::debug::LOG_DEBUG, __FILE__, __FUNCTION__, __LINE__,__VA_ARGS__)
#   define manapi_log_ffatal2(...) manapi::debug::flogit(manapi::debug::LOG_FATAL, __FILE__, __FUNCTION__, __LINE__,__VA_ARGS__)
#else
#   define manapi_log_ferror(...) manapi::debug::flogit(manapi::debug::LOG_ERROR, __FILE__, __func__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_ftrace(...) manapi::debug::flogit(manapi::debug::LOG_TRACE, __FILE__, __func__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_finfo(...) manapi::debug::flogit(manapi::debug::LOG_INFO, __FILE__, __func__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_fwarn(...) manapi::debug::flogit(manapi::debug::LOG_WARN, __FILE__, __func__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_fdebug(...) manapi::debug::flogit(manapi::debug::LOG_DEBUG, __FILE__, __func__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_ffatal(...) manapi::debug::flogit(manapi::debug::LOG_FATAL, __FILE__, __func__, __LINE__, LOGNAME,__VA_ARGS__)
#   define manapi_log_ferror2(...) manapi::debug::flogit(manapi::debug::LOG_ERROR, __FILE__, __func__, __LINE__,__VA_ARGS__)
#   define manapi_log_ftrace2(...) manapi::debug::flogit(manapi::debug::LOG_TRACE, __FILE__, __func__, __LINE__,__VA_ARGS__)
#   define manapi_log_finfo2(...) manapi::debug::flogit(manapi::debug::LOG_INFO, __FILE__, __func__, __LINE__,__VA_ARGS__)
#   define manapi_log_fwarn2(...) manapi::debug::flogit(manapi::debug::LOG_WARN, __FILE__, __func__, __LINE__,__VA_ARGS__)
#   define manapi_log_fdebug2(...) manapi::debug::flogit(manapi::debug::LOG_DEBUG, __FILE__, __func__, __LINE__,__VA_ARGS__)
#   define manapi_log_ffatal2(...) manapi::debug::flogit(manapi::debug::LOG_FATAL, __FILE__, __func__, __LINE__,__VA_ARGS__)
#endif
}