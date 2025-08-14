#include <cstdarg>
#include <ctime>
#include <cstring>

#include "ManapiErrors.hpp"
#include "ManapiDebug.hpp"
#include "json/ManapiJson.hpp"
#include "std/ManapiAsyncContext.hpp"
#include "./include/ManapiUtils.hpp"

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

manapi::exception::messages::~messages() {}

manapi::exception::exception(manapi::err_num errnum, std::string message) : data_() {
    this->errnum_ = errnum;
    this->flags = 0;
    new (&this->data_.storage) std::string(std::move(message));
}

manapi::exception::exception(manapi::err_num errnum, std::string_view message) {
    this->flags = 1;
    this->errnum_ = errnum;
    new (&this->data_.view) std::string_view((message));
}

manapi::exception::exception(manapi::err_num errnum, const char *message) : data_() {
    this->flags = 1;
    this->errnum_ = errnum;

    if (!message)
        message = "null";

    new (&this->data_.view) std::string_view((message));
}

manapi::exception::exception(const exception &n) : data_() {
    this->operator=(n);
}

manapi::exception & manapi::exception::operator=(const exception &n) {
    this->flags = n.flags;
    this->errnum_ = n.errnum_;

    if (n.flags)
        new (&this->data_.view) std::string_view(n.data_.view);
    else {
        try {
            auto s = n.data_.storage;
            new (&this->data_.storage) std::string (std::move(s));
        }
        catch (std::exception const &e) {
            this->flags = 0;
            new (&this->data_.view) std::string_view ("exception: failed to copy message");
            manapi_log_trace("%s due to %s", "exception: failed to copy message", e.what());
        }
    }

    return *this;
}

manapi::exception::~exception() {
    if (this->flags) {
        this->data_.view.~basic_string_view();
    }
    else {
        this->data_.storage.~basic_string();
    }
}


const char *manapi::exception::what() const noexcept {
    if (this->flags)
        return this->data_.view.data();
    return this->data_.storage.data();
}

int manapi::exception::err_num() const {
    return this->errnum_;
}

manapi::error::status::status() {
    this->code_ = manapi::ERR_OK;
}

manapi::error::status::~status() = default;

manapi::error::status::status(err_num code, std::string_view msg) {
    this->code_ = code;
    this->msg_ = msg;
}

manapi::error::status::status(status &&n) noexcept = default;

manapi::error::status & manapi::error::status::operator=(status &&n) noexcept = default;

manapi::error::status::status(const status &n) = default;

manapi::error::status & manapi::error::status::operator=(const status &n) = default;

std::string_view manapi::error::status::msg() const {
    return this->msg_;
}

manapi::err_num manapi::error::status::code() const {
    return this->code_;
}


bool manapi::error::status::ok() const {
    return this->code_ == manapi::ERR_OK;
}

void manapi::error::status::log() const {
    MANAPIHTTP_LOG ("{}: msg: {}", this->status_msg(), this->msg_);
}

std::string_view manapi::error::status::status_msg() const {
    return get_msg_by_err_num(this->code_);
}

void manapi::error::status::unwrap() const {
    if (this->code_ != ERR_OK)
        THROW_MANAPIHTTP_EXCEPTION(this->code_, "{}: msg: {}", this->status_msg(), this->msg_);
}

manapi::error::status::operator bool() const noexcept(true) {
    return this->code_ == ERR_OK;
}

manapi::error::status manapi::error::status_ok() {
    return {};
}

manapi::error::status manapi::error::status_unknown(std::string_view msg) {
    return {ERR_UNKNOWN, msg};
}

manapi::error::status manapi::error::status_cancelled() {
    return {ERR_CANCELLED, {"cancelled"}};
}

manapi::error::status manapi::error::status_cancelled(std::string_view msg) {
    return {ERR_CANCELLED, msg};
}

manapi::error::status manapi::error::status_invalid_argument(std::string_view msg) {
    return {ERR_INVALID_ARGUMENT, msg};
}

manapi::error::status manapi::error::status_deadline_exceeded(std::string_view msg) {
    return {ERR_DEADLINE_EXCEEDED, msg};
}

manapi::error::status manapi::error::status_not_found(std::string_view msg) {
    return {ERR_NOT_FOUND, msg};
}

manapi::error::status manapi::error::status_already_exists(std::string_view msg) {
    return {ERR_ALREADY_EXISTS, msg};
}

manapi::error::status manapi::error::status_already_exists() {
    return error::status_already_exists("already exists");
}

manapi::error::status manapi::error::status_permission_denied(std::string_view msg) {
    return {ERR_PERMISSION_DENIED, msg};
}

manapi::error::status manapi::error::status_unauthenticated(std::string_view msg) {
    return {ERR_UNAUTHENTICATED, msg};
}

manapi::error::status manapi::error::status_resource_exhausted() {
    return {ERR_RESOURCE_EXHAUSTED, "bad alloc"};
}

manapi::error::status manapi::error::status_resource_exhausted(std::string_view msg) {
    return {ERR_RESOURCE_EXHAUSTED, msg};
}

manapi::error::status manapi::error::status_failed_precondition(std::string_view msg) {
    return {ERR_FAILED_PRECONDITION, msg};
}

manapi::error::status manapi::error::status_aborted(std::string_view msg) {
    return {ERR_ABORTED, msg};
}

manapi::error::status manapi::error::status_unavailable(std::string_view msg) {
    return {ERR_UNAVAILABLE, msg};
}

manapi::error::status manapi::error::status_out_of_range(std::string_view msg) {
    return {ERR_OUT_OF_RANGE, msg};
}

manapi::error::status manapi::error::status_unimplemented(std::string_view msg) {
    return {ERR_UNIMPLEMENTED, msg};
}

manapi::error::status manapi::error::status_internal(std::string_view msg) {
    return {ERR_INTERNAL, msg};
}

manapi::error::status manapi::error::status_internal() {
    return error::status_internal("failed");
}

manapi::error::status manapi::error::status_data_loss(std::string_view msg) {
    return {ERR_DATA_LOSS, msg};
}

void log_log_ (manapi::debug::log_level type, int level, const char *file, int line, const char *fmt, va_list args) {
    if (type == manapi::debug::LOG_TRACE && level > manapi::debug::log_trace_enabled)
        return;


    // Remove path from filename
    const char* base = strrchr(file, '/');
    if (!base) base = strrchr(file, '\\');
    base = base ? base + 1 : file;

    std::lock_guard<std::mutex> lk (log_mx);
    // Print timestamp, log level, and file info
    fprintf(
        stderr,
        "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ",
        manapi::debug::level_colors[type],
        manapi::debug::level_colors[type],
        manapi::debug::level_strings[type],
        base,
        line
    );

    // Print user message
    vfprintf(stderr, fmt, args);

    // Newline and flush
    fprintf(stderr, "\n");
    fflush(stderr);
}

void manapi::debug::log_log(log_level type, const char *file, int line, const char *fmt, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    log_log_(type, LOG_TRACE_HIGH, file, line, fmt, args);
    va_end(args);
}

void manapi::debug::log_log(log_level type, const char *file, int line, int level, const char *fmt, ...) MANAPIHTTP_NOEXCEPT {
    va_list args;
    va_start(args, fmt);
    log_log_(type, level, file, line, fmt, args);
    va_end(args);
}
