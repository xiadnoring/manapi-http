#include <cstdarg>
#include <ctime>
#include <cstring>

#include "ManapiErrors.hpp"
#include "ManapiDebug.hpp"
#include "json/ManapiJson.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "./include/ManapiUtils.hpp"

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


int manapi::debug::log_trace_enabled = -1;

static std::mutex log_mx;

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
    if (this->m_errnum & (1<<31)) {
        this->m_data.m_str.~basic_string();
    }
}

manapi::messages::messages(messages &&n) MANAPIHTTP_NOEXCEPT {
    this->m_errnum = std::exchange(n.m_errnum, 0);
    if (this->m_errnum & (1<<31)) {
        new (&this->m_data.m_str) std::string (std::move(n.m_data.m_str));
        n.m_data.m_str.~basic_string();
    }
    else {
        this->m_data.m_view = n.m_data.m_view;
    }
}

manapi::messages & manapi::messages::operator=(messages &&n) MANAPIHTTP_NOEXCEPT {
    if (this != &n) {
        this->m_errnum = std::exchange(n.m_errnum, 0);
        if (this->m_errnum & (1<<31)) {
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
    if (this->m_errnum & (1<<31)) {
        new (&this->m_data.m_str) std::string (n.m_data.m_str);
    }
    else {
        this->m_data.m_view = n.m_data.m_view;
    }
}

manapi::messages & manapi::messages::operator=(const messages &n) {
    if (this != &n) {
        this->m_errnum = n.m_errnum;
        if (this->m_errnum & (1<<31)) {
            new (&this->m_data.m_str) std::string (n.m_data.m_str);
        }
        else {
            this->m_data.m_view = n.m_data.m_view;
        }
    }
    return *this;
}

void manapi::messages::errnum(manapi::err_num code) MANAPIHTTP_NOEXCEPT {
    this->m_errnum = static_cast<uint32_t> (code)|(this->m_errnum & (1<<31));
}

manapi::err_num manapi::messages::errnum() const MANAPIHTTP_NOEXCEPT {
    if (this->m_errnum & (1<<31)) return static_cast<manapi::err_num>(this->m_errnum ^ (1<<31));
    return static_cast<manapi::err_num>(this->m_errnum);
}

std::string_view manapi::messages::msg_view() const MANAPIHTTP_NOEXCEPT {
    if (this->m_errnum & (1<<31)) return this->m_data.m_str;
    return this->m_data.m_view;
}

std::string manapi::messages::msg() MANAPIHTTP_NOEXCEPT {
    if (this->m_errnum & (1<<31)) return std::move(this->m_data.m_str);
    return std::string{this->m_data.m_view};
}

void manapi::messages::msg_view(std::string_view msg) MANAPIHTTP_NOEXCEPT {
    if (this->m_errnum & (1<<31)) {
        this->m_data.m_str.~basic_string();
        this->m_errnum ^= (1<<31);
    }
    this->m_data.m_view = msg;
}

void manapi::messages::msg(std::string msg) MANAPIHTTP_NOEXCEPT {
    if (this->m_errnum & (1<<31)) {
        this->m_data.m_str = std::move(msg);
        return;
    }
    new (&this->m_data.m_str) std::string(std::move(msg));
    this->m_errnum |= (1<<31);
}

manapi::exception::exception(manapi::err_num errnum, std::string message) {
    this->m_data.errnum(errnum);
    this->m_data.msg(std::move(message));
}

manapi::exception::exception(manapi::err_num errnum, std::string_view message) {
    this->m_data.errnum(errnum);
    this->m_data.msg_view(message);
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

manapi::status::status(err_num code, std::string_view msg) {
    this->m_data.errnum(code);
    this->m_data.msg_view(msg);
}

manapi::status::status(status &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::status & manapi::status::operator=(status &&n) MANAPIHTTP_NOEXCEPT = default;

manapi::status::status(const status &n) = default;

manapi::status & manapi::status::operator=(const status &n) = default;

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
    print_stacktrace(2);
    MANAPIHTTP_LOG ("{}: msg: {}", this->status_msg(), this->m_data.msg_view());
}

std::string_view manapi::status::status_msg() const {
    return get_msg_by_err_num(this->m_data.errnum());
}

void manapi::status::unwrap() const {
    if (this->m_data.errnum() != ERR_OK)
        THROW_MANAPIHTTP_EXCEPTION(this->m_data.errnum(), "{}: msg: {}", this->status_msg(), this->m_data.msg_view());
}

void manapi::status::stacktrace() const MANAPIHTTP_NOEXCEPT {
    print_stacktrace();
}

manapi::status::operator bool() const MANAPIHTTP_NOEXCEPT {
    return this->m_data.errnum() == ERR_OK;
}

manapi::status manapi::status_ok() {
    return {};
}

manapi::status manapi::status_unknown(std::string_view msg) {
    return {ERR_UNKNOWN, msg};
}

manapi::status manapi::status_cancelled() {
    return {ERR_CANCELLED, {"cancelled"}};
}

manapi::status manapi::status_cancelled(std::string_view msg) {
    return {ERR_CANCELLED, msg};
}

manapi::status manapi::status_invalid_argument(std::string_view msg) {
    return {ERR_INVALID_ARGUMENT, msg};
}

manapi::status manapi::status_deadline_exceeded(std::string_view msg) {
    return {ERR_DEADLINE_EXCEEDED, msg};
}

manapi::status manapi::status_not_found(std::string_view msg) {
    return {ERR_NOT_FOUND, msg};
}

manapi::status manapi::status_already_exists(std::string_view msg) {
    return {ERR_ALREADY_EXISTS, msg};
}

manapi::status manapi::status_already_exists() {
    return status_already_exists("already exists");
}

manapi::status manapi::status_permission_denied(std::string_view msg) {
    return {ERR_PERMISSION_DENIED, msg};
}

manapi::status manapi::status_unauthenticated(std::string_view msg) {
    return {ERR_UNAUTHENTICATED, msg};
}

manapi::status manapi::status_resource_exhausted() {
    return {ERR_RESOURCE_EXHAUSTED, "bad alloc"};
}

manapi::status manapi::status_resource_exhausted(std::string_view msg) {
    return {ERR_RESOURCE_EXHAUSTED, msg};
}

manapi::status manapi::status_failed_precondition(std::string_view msg) {
    return {ERR_FAILED_PRECONDITION, msg};
}

manapi::status manapi::status_aborted(std::string_view msg) {
    return {ERR_ABORTED, msg};
}

manapi::status manapi::status_unavailable(std::string_view msg) {
    return {ERR_UNAVAILABLE, msg};
}

manapi::status manapi::status_out_of_range(std::string_view msg) {
    return {ERR_OUT_OF_RANGE, msg};
}

manapi::status manapi::status_unimplemented(std::string_view msg) {
    return {ERR_UNIMPLEMENTED, msg};
}

manapi::status manapi::status_internal(std::string_view msg) {
    return {ERR_INTERNAL, msg};
}

manapi::status manapi::status_internal() {
    return status_internal("failed");
}

manapi::status manapi::status_data_loss(std::string_view msg) {
    return {ERR_DATA_LOSS, msg};
}

void logit_ (manapi::debug::log_level type, int level, const char *file, const char *func, int line, const char *fmt, va_list args) {
    if (type == manapi::debug::LOG_TRACE && level > manapi::debug::log_trace_enabled)
        return;

    auto timepoint = std::chrono::system_clock::now();
    auto coarse = std::chrono::system_clock::to_time_t(timepoint);
    auto fine = std::chrono::time_point_cast<std::chrono::milliseconds>(timepoint);

    char tstr[sizeof "9999-12-31 23:59:59.999"];
    std::snprintf(tstr + std::strftime(tstr, sizeof tstr - 3,
                                         "%F %T.", std::localtime(&coarse)),
                  4, "%03lu", fine.time_since_epoch().count() % 1000);

    // Remove path from filename
    const char* base = strrchr(file, '/');
    if (!base) base = strrchr(file, '\\');
    base = base ? base + 1 : file;

    std::lock_guard<std::mutex> lk (log_mx);
    if (func) {
        // Print timestamp, log level, and file info
        fprintf(
            stderr,
            "%s%-5s\x1b[0m \x1b[90m%s %s:%d(%s):\x1b[0m ",
            level_colors[type],
            level_strings[type],
            tstr,
            base,
            line,
            func
        );
    }
    else {
        // Print timestamp, log level, and file info
        fprintf(
            stderr,
            "%s%-5s\x1b[0m \x1b[90m%s %s:%d:\x1b[0m ",
            level_colors[type],
            level_strings[type],
            tstr,
            base,
            line
        );
    }

    // Print user message
    vfprintf(stderr, fmt, args);

    // Newline and flush
    fprintf(stderr, "\n");
    fflush(stderr);
}

void manapi::debug::logit(log_level type, const char *file, int line, const char *fmt, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    logit_(type, LOG_TRACE_HIGH, file, nullptr, line, fmt, args);
    va_end(args);
}

void manapi::debug::logit(log_level type, const char *file, int line, int level, const char *fmt, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    logit_(type, level, file, nullptr, line, fmt, args);
    va_end(args);
}

void manapi::debug::flogit(log_level type, const char *file, const char *func, int line, const char *fmt,...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    logit_(type, LOG_TRACE_HIGH, file, func, line, fmt, args);
    va_end(args);
}

void manapi::debug::flogit(log_level type, const char *file, const char *func, int level, int line, const char *fmt,...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    logit_(type, LOG_TRACE_HIGH, file, func, line, fmt, args);
    va_end(args);
}

