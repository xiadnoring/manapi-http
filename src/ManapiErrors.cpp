#include <cstdarg>
#include <ctime>
#include <cstring>
#include <unordered_map>

#include "ManapiErrors.hpp"
#include "ManapiDebug.hpp"
#include "json/ManapiJson.hpp"
#include "std/ManapiContext.hpp"
#include "./include/ManapiUtils.hpp"

static const char* manapi__level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static std::tm manapi__localtime_xp(std::time_t timer)
{
    std::tm bt {};
#if defined(__unix__)
    localtime_r(&timer, &bt);
#elif defined(_MSC_VER)
    localtime_s(&bt, &timer);
#else
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    bt = *std::localtime(&timer);
#endif
    return bt;
}

#ifdef LOG_NO_COLOR
static const char* level_colors[] = {
    "", "", "", "", "", ""
};
#else
static const char* level_colors[] = {
    "\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"
};
#endif

enum manapi__log_trace__flags {
    MANAPI__LOG_TRACE__FLAG_ENABLE_ALL = 1<<0
};

static int manapi__log_trace_enabled = 0;

static int manapi__log_trace_flags = 0;

static std::mutex manapi__log_mx;

static std::unordered_set<std::string, manapi::text_hash, std::equal_to<>> manapi__log_names_enabled;

void manapi::debug::set_log_name_enabled(const char *name, bool enabled) {
    auto sv = std::string_view(name);


    if (sv == "all") {
        if (enabled) {
            ::manapi__log_trace_flags |= MANAPI__LOG_TRACE__FLAG_ENABLE_ALL;
        }
        else {
            if (::manapi__log_trace_flags & MANAPI__LOG_TRACE__FLAG_ENABLE_ALL)
                ::manapi__log_trace_flags ^= MANAPI__LOG_TRACE__FLAG_ENABLE_ALL;
        }
    }
    else {
        if (enabled) {
            ::manapi__log_names_enabled.insert(std::string(sv));
        }
        else {
            auto it = ::manapi__log_names_enabled.find(sv);
            if (it != ::manapi__log_names_enabled.end())
                ::manapi__log_names_enabled.erase(it);
        }
    }
}

void manapi::debug::set_log_trace_enabled (int value) MANAPIHTTP_NOEXCEPT {
    ::manapi__log_trace_enabled = value;
}

std::string_view manapi::get_msg_by_err_num (manapi::err_num err) {
    switch (err) {
        case manapi::ERR_OK: return "ERR_OK";
        case manapi::ERR_ABORTED: return "ERR_ABORTED";
        case manapi::ERR_UNKNOWN: return "ERR_UNKNOWN";
        case manapi::ERR_DATA_LOSS: return "ERR_DATA_LOSS";
        case manapi::ERR_INTERNAL: return "ERR_INTERNAL";
        case manapi::ERR_NOT_FOUND: return "ERR_NOT_FOUND";
        case manapi::ERR_CANCELLED: return "ERR_CANCELLED";
        case manapi::ERR_OUT_OF_RANGE: return "ERR_OUT_OF_RANGE";
        case manapi::ERR_UNAVAILABLE: return "ERR_UNAVAILABLE";
        case manapi::ERR_ALREADY_EXISTS: return "ERR_ALREADY_EXISTS";
        case manapi::ERR_UNIMPLEMENTED: return "ERR_UNIMPLEMENTED";
        case manapi::ERR_INVALID_ARGUMENT: return "ERR_INVALID_ARGUMENT";
        case manapi::ERR_UNAUTHENTICATED: return "ERR_UNAUTHENTICATED";
        case manapi::ERR_DEADLINE_EXCEEDED: return "ERR_DEADLINE_EXCEEDED";
        case manapi::ERR_PERMISSION_DENIED: return "ERR_PERMISSION_DENIED";
        case manapi::ERR_RESOURCE_EXHAUSTED: return "ERR_RESOURCE_EXHAUSTED";
        case manapi::ERR_FAILED_PRECONDITION: return "ERR_FAILED_PRECONDITION";
    }

    return "ERR_UNKNOWN";
}

void manapi::extract_exception_ptr(std::exception_ptr err, int *errnum, std::string *msg) {
    try {
        std::rethrow_exception(std::move(err));
    }
    catch (manapi::exception &e) {
        if (errnum)
            *errnum = e.err_num();

        if (msg)
            *msg = e.what();
    }
    catch (std::exception const &e) {
        if (errnum)
            *errnum = ERR_UNKNOWN;

        if (msg)
            *msg = e.what();
    }
}

void manapi::extract_exception_ptr(std::exception_ptr err, int *errnum, char *msg, std::size_t *msg_size) {
    try {
        std::rethrow_exception(std::move(err));
    }
    catch (manapi::exception &e) {
        if (errnum)
            *errnum = e.err_num();

        if (msg && msg_size) {
            *msg_size = std::min<std::size_t>(*msg_size, strlen(e.what()));
            memcpy (msg, e.what(), *msg_size);
        }
    }
    catch (std::exception const &e) {
        if (errnum)
            *errnum = ERR_UNKNOWN;

        if (msg && msg_size) {
            *msg_size = std::min<std::size_t>(*msg_size, strlen(e.what()));
            memcpy (msg, e.what(), *msg_size);
        }
    }
}

manapi::messages_storage::~messages_storage() {
}

manapi::messages::messages() {
    this->m_errnum = 0;
}

manapi::messages::~messages() {
    if (this->m_errnum & (1U<<31)) {
        this->m_data.m_str.~basic_string();
    }
    else {
        this->m_data.m_view.~basic_string_view();
    }
}

manapi::messages::messages(messages &&n) MANAPIHTTP_NOEXCEPT {
    this->m_errnum = std::exchange(n.m_errnum, 0);
    if (this->m_errnum & (1U<<31)) {
        new (&this->m_data.m_str) std::string (std::move(n.m_data.m_str));
        n.m_data.m_str.~basic_string();
    }
    else {
        this->m_data.m_view = n.m_data.m_view;
    }
}

manapi::messages & manapi::messages::operator=(messages &&n) MANAPIHTTP_NOEXCEPT {
    if (this != &n) {
        if (this->m_errnum & (1U<<31)) this->m_data.m_str.~basic_string();
        this->m_errnum = std::exchange(n.m_errnum, 0);
        if (this->m_errnum & (1U<<31)) {
            new (&this->m_data.m_str) std::string (std::move(n.m_data.m_str));
            n.m_data.m_str.~basic_string();
        }
        else {
            this->m_data.m_view = n.m_data.m_view;
        }
    }
    return *this;
}

manapi::messages::messages(const messages &n) {
    this->m_errnum = n.m_errnum;
    if (this->m_errnum & (1U<<31)) {
        new (&this->m_data.m_str) std::string (n.m_data.m_str);
    }
    else {
        this->m_data.m_view = n.m_data.m_view;
    }
}

manapi::messages & manapi::messages::operator=(const messages &n) {
    if (this != &n) {
        if (this->m_errnum & (1U<<31)) this->m_data.m_str.~basic_string();
        this->m_errnum = n.m_errnum;
        if (this->m_errnum & (1U<<31)) {
            new (&this->m_data.m_str) std::string (n.m_data.m_str);
        }
        else {
            this->m_data.m_view = n.m_data.m_view;
        }
    }
    return *this;
}

void manapi::messages::errnum(manapi::err_num code) MANAPIHTTP_NOEXCEPT {
    this->m_errnum = static_cast<uint32_t> (code)|(this->m_errnum & (1U<<31));
}

manapi::err_num manapi::messages::errnum() const MANAPIHTTP_NOEXCEPT {
    if (this->m_errnum & (1U<<31)) return static_cast<manapi::err_num>(this->m_errnum ^ (1U<<31));
    return static_cast<manapi::err_num>(this->m_errnum);
}

std::string_view manapi::messages::msg_view() const MANAPIHTTP_NOEXCEPT {
    if (this->m_errnum & (1U<<31)) return this->m_data.m_str;
    return this->m_data.m_view;
}

std::string manapi::messages::msg() MANAPIHTTP_NOEXCEPT {
    if (this->m_errnum & (1U<<31)) return std::move(this->m_data.m_str);
    return std::string{this->m_data.m_view};
}

void manapi::messages::msg_view(std::string_view msg) MANAPIHTTP_NOEXCEPT {
    if (this->m_errnum & (1U<<31)) {
        this->m_data.m_str.~basic_string();
        this->m_errnum ^= (1U<<31);
    }
    this->m_data.m_view = msg;
}

void manapi::messages::msg(std::string msg) MANAPIHTTP_NOEXCEPT {
    if (this->m_errnum & (1U<<31)) {
        this->m_data.m_str = std::move(msg);
        return;
    }
    new (&this->m_data.m_str) std::string(std::move(msg));
    this->m_errnum |= (1U<<31);
}

manapi::exception::exception(messages msg) {
    this->m_data = std::move(msg);
}

manapi::exception::exception(manapi::err_num errnum, std::string message) {
    this->m_data.errnum(errnum);
    this->m_data.msg(std::move(message));
}

manapi::exception::exception(manapi::err_num errnum, std::string_view message) {
    this->m_data.errnum(errnum);
    this->m_data.msg_view(message);
}

manapi::exception::exception(manapi::err_num errnum, std::string_view fmt, ...) {
    std::string message;

    va_list args;
    va_start(args, fmt);

    try {
        va_list args_copy;
        va_copy(args_copy, args);
        const int size = vsnprintf(nullptr, 0, fmt.data(), args_copy);
        va_end(args_copy);

        message.resize(static_cast<std::size_t>(size));
        vsnprintf(message.data(), message.size() + 1, fmt.data(), args);
    }
    catch (std::exception const &e) {
        manapi_log_trace(manapi::debug::LOG_TRACE_LOW, "exception failed due to %s",
            e.what());
    }

    va_end(args);

    this->m_data.errnum(errnum);
    this->m_data.msg(std::move(message));
}

manapi::exception::exception(manapi::err_num errnum, const char *message) {
    this->m_data.errnum(errnum);
    this->m_data.msg_view(message);
}

manapi::exception::exception(const exception &n) {
    this->m_data = n.m_data;
}

manapi::exception & manapi::exception::operator=(const exception &n) {
    if (this != &n) {
        this->m_data = n.m_data;
    }
    return *this;
}

manapi::exception::~exception() = default;


const char *manapi::exception::what() const MANAPIHTTP_NOEXCEPT {
    return this->m_data.msg_view().data();
}

manapi::err_num manapi::exception::err_num() const {
    return this->m_data.errnum();
}

manapi::status::status() {
    this->m_data.errnum(ERR_OK);
}

manapi::status::~status() = default;

manapi::status::status(messages msg) {
    this->m_data = std::move(msg);
}

manapi::status::status(err_num code, const char *msg) {
    this->m_data.errnum(code);
    this->m_data.msg_view(msg);
}

manapi::status::status(err_num code, std::string_view msg) {
    this->m_data.errnum(code);
    this->m_data.msg_view(msg);
}

manapi::status::status(err_num code, std::string msg) {
    this->m_data.errnum(code);
    this->m_data.msg(std::move(msg));
}

manapi::status::status(status &&n) MANAPIHTTP_NOEXCEPT {
    this->m_data = std::move(n.m_data);
}

manapi::status & manapi::status::operator=(status &&n) MANAPIHTTP_NOEXCEPT {
    if (this != &n) {
        this->m_data = std::move(n.m_data);
    }
    return *this;
}

manapi::status::status(const status &n) {
    this->m_data = (n.m_data);
}

manapi::status & manapi::status::operator=(const status &n) {
    if (this != &n) {
        this->m_data = n.m_data;
    }
    return *this;
}

std::string_view manapi::status::msg() const {
    return this->m_data.msg_view();
}

manapi::err_num manapi::status::code() const {
    return this->m_data.errnum();
}


bool manapi::status::ok() const {
    return this->m_data.errnum() == manapi::ERR_OK;
}

void manapi::status::log() const {
    auto s = this->fullmsg();
    print_stacktrace(2);
    manapi_log_debug(s.data());
}

std::string_view manapi::status::status_msg() const {
    return get_msg_by_err_num(this->m_data.errnum());
}

void manapi::status::unwrap() const {
    if (this->m_data.errnum() != ERR_OK)
        throw manapi::exception(this->m_data.errnum(), this->fullmsg());
}

std::string manapi::status::fullmsg() const {
    std::string_view msg = this->m_data.msg_view();
    if (msg.empty()) msg = "EMPTY";
    return std::format("{}: msg={}", this->status_msg(), msg);
}

void manapi::status::stacktrace() const MANAPIHTTP_NOEXCEPT {
    print_stacktrace();
}

manapi::status::operator bool() const MANAPIHTTP_NOEXCEPT {
    return this->m_data.errnum() == ERR_OK;
}

void manapi::status::data(messages data) {
    this->m_data = std::move(data);
}

manapi::messages manapi::status::data() {
    return std::move(this->m_data);
}

manapi::messages manapi::status::copy_data() {
    return this->m_data;
}

static void manapi__log (manapi::debug::log_level type, int level, const char *file, const char *func, int line, const char *name, const char *fmt, va_list args) {
    if (type == manapi::debug::LOG_TRACE ) {
        if ( !(manapi__log_trace_flags & MANAPI__LOG_TRACE__FLAG_ENABLE_ALL) && !::manapi__log_names_enabled.contains(std::string_view (name)))
            return;
        if (level > manapi__log_trace_enabled)
            return;
    }

    auto timepoint = std::chrono::system_clock::now();
    auto coarse = std::chrono::system_clock::to_time_t(timepoint);
    auto fine = std::chrono::time_point_cast<std::chrono::milliseconds>(timepoint);
    auto txp = manapi__localtime_xp(coarse);
    char tstr[sizeof "9999-12-31 23:59:59.999"];
    std::snprintf(tstr + std::strftime(tstr, sizeof tstr - 3,
                                         "%F %T.", &txp),
                  4, "%03llu", static_cast<unsigned long long>(fine.time_since_epoch().count() % 1000));

    // Remove path from filename
    const char* base = strrchr(file, '/');
    if (!base) base = strrchr(file, '\\');
    base = base ? base + 1 : file;

    std::lock_guard<std::mutex> lk (manapi__log_mx);
    if (func) {
        // Print timestamp, log level, and file info
        fprintf(
            stdout,
            "%s%-5s\x1b[0m \x1b[90m%s %s:%d(%s):\x1b[0m ",
            level_colors[type],
            manapi__level_strings[type],
            tstr,
            base,
            line,
            func
        );
    }
    else {
        // Print timestamp, log level, and file info
        fprintf(
                stdout,
            "%s%-5s\x1b[0m \x1b[90m%s %s:%d:\x1b[0m ",
            level_colors[type],
            manapi__level_strings[type],
            tstr,
            base,
            line
        );
    }

    // Print user message
    vfprintf(stdout, fmt, args);

    // Newline and flush
    fprintf(stdout, "\n");
    fflush(stdout);
}

void manapi::debug::logit(log_level type, const char *file, int line, const char *name, const char *fmt, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    ::manapi__log(type, LOG_TRACE_HIGH, file, nullptr, line, name, fmt, args);
    va_end(args);
}

void manapi::debug::logit(log_level type, const char *file, int line, const char *name, int level, const char *fmt, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    ::manapi__log(type, level, file, nullptr, line, name, fmt, args);
    va_end(args);
}

void manapi::debug::flogit(log_level type, const char *file, const char *func, int line, const char *name, const char *fmt,...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    ::manapi__log(type, LOG_TRACE_HIGH, file, func, line, name, fmt, args);
    va_end(args);
}

void manapi::debug::flogit(log_level type, const char *file, const char *func, int line, const char *name, int level, const char *fmt,...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    ::manapi__log(type, LOG_TRACE_HIGH, file, func, line, name, fmt, args);
    va_end(args);
}

